module Ferret::Search
  # MultiPhraseQuery is a generalized version of PhraseQuery, with an added
  # method #add(Term[]).
  #
  # To use this class, to search for the phrase "Microsoft app*" first use
  # add(Term) on the term "Microsoft", then find all terms that have "app" as
  # prefix using IndexReader.terms(Term), and use MultiPhraseQuery.add(Term[]
  # terms) to add them to the query.
  # 
  # Author Anders Nielsen
  class MultiPhraseQuery < Query 
    include Ferret::Index

    attr_accessor :slop
    attr_reader :positions, :term_arrays, :field

    def initialize()
      super()
      @slop = 0
      @term_arrays = []
      @positions = []
      @field = nil
    end

    # Allows to specify the relative position of terms within the phrase.
    # 
    # See PhraseQuery#add(Term, int)
    # terms:: the array of terms to search for or a single term
    # position:: the position to search for these terms
    def add(terms, position = nil, pos_inc = 1) 
      if position.nil?
        position = (@positions.size > 0) ? (@positions[-1] + pos_inc) : 0
      end

      if terms.instance_of?(Term)
        terms = [terms]
      end

      if (@term_arrays.size == 0)
        @field = terms[0].field
      end

      terms.each do |term|
        if (term.field != @field) 
          raise ArgumentError,
            "All phrase terms must be in the same field (#{@field}): #{term}"
        end
      end

      if i = @positions.index(position)
        term_arrays[i] += terms
      else
        @term_arrays << terms
        @positions << position
      end
    end
    alias :<< :add
    
    class MultiPhraseWeight < Weight 
      include Ferret::Index

      attr_reader :query, :value

      def initialize(query, searcher)
        @query = query
        @term_arrays = query.term_arrays
        @positions = query.positions
        @similarity = query.similarity(searcher)
        @idf = 0.0

        # compute idf
        query.term_arrays.each do |terms|
          terms.each do |term|
            @idf += @similarity.idf_term(term, searcher)
          end
        end
      end

      def sum_of_squared_weights() 
        @query_weight = @idf * @query.boost()      # compute query weight
        return @query_weight * @query_weight       # square it
      end

      def normalize(query_norm) 
        @query_norm = query_norm
        @query_weight *= query_norm                # normalize query weight
        @value = @query_weight * @idf              # idf for document 
      end

      def scorer(reader)
        return nil if (@term_arrays.size == 0) # optimize zero-term case
        tps = []
        @term_arrays.each do |terms|
          p = []
          if (terms.length > 1)
            p = MultipleTermDocPosEnum.new(reader, terms)
          else
            p = reader.term_positions_for(terms[0])
          end
        
          return nil if (p == nil)
        
          tps << p
        end
      
        if (@query.slop == 0)
          return ExactPhraseScorer.new(self, tps, @positions, @similarity,
                                       reader.get_norms(@query.field))
        else
          return SloppyPhraseScorer.new(self, tps, @positions, @similarity,
                                        @query.slop, reader.get_norms(@query.field))
        end
      end
      
      def explain(reader, doc)
       
        result = Explanation.new()
        result.description = "weight(#{@query} in #{doc}), product of:"

        idf_expl = Explanation.new(@idf, "idf(#{@query})")
        
        # explain query weight
        query_expl = Explanation.new()
        query_expl.description = "query_weight(#{@query}), product of:"

        boost = @query.boost()
        if boost != 1.0
          boost_expl = Explanation.new(boost, "boost")
          query_expl << boost_expl
        end
        query_expl << idf_expl
        
        query_norm_expl = Explanation.new(@query_norm,"query_norm")
        query_expl << query_norm_expl
        
        query_expl.value = boost * @idf * @query_norm

        result << query_expl
       
        # explain field weight
        field_expl = Explanation.new()
        field_expl.description =
          "field_weight(#{@query} in #{doc}), product of:"

        tf_expl = scorer(reader).explain(doc)
        field_expl << tf_expl
        field_expl << idf_expl

        field_norm_expl = Explanation.new()
        field_norms = reader.get_norms(@query.field)
        field_norm =
          field_norms ? Similarity.decode_norm(field_norms[doc]) : 0.0
        field_norm_expl.value = field_norm
        field_norm_expl.description =
          "field_norm(field=#{@query.field}, doc=#{doc})"
        field_expl << field_norm_expl

        field_expl.value = tf_expl.value * idf_expl.value * field_norm_expl.value 
        result << field_expl

        if (query_expl.value == 1.0)
          return field_expl
        else
          result.value = query_expl.value * field_expl.value
          return result
        end
      end
    end

    def rewrite(reader) 
      if (@term_arrays.size() == 1) # optimize one-term case
        terms = @term_arrays[0]
        bq = BooleanQuery.new(true)
        terms.each do |term|
          bq.add_query(TermQuery.new(term), BooleanClause::Occur::SHOULD)
        end
        bq.boost = boost()
        return bq
      else 
        return self
      end
    end
    
    # See Query#extract_terms()
    def extract_terms(query_terms) 
      @term_arrays.each { |terms| 
        query_terms.merge(terms) 
      }
    end

    def create_weight(searcher)
      return MultiPhraseWeight.new(self, searcher)
    end

    # Prints a user-readable version of this query. 
    def to_s(f = nil) 
      buffer = ""
      buffer << "#{@field}:" if @field != f 
      buffer << '"'
      last_pos = -1
      @term_arrays.each_index do |i|
        terms = @term_arrays[i]
        pos = @positions[i]
        last_pos.upto(pos-2) {buffer << "<> "}
        last_pos = pos
        buffer << "#{terms.map {|term| term.text}.join("|")} "
      end
      buffer.rstrip!
      buffer << '"'

      buffer << "~#{@slop}" if (@slop != 0) 
      buffer << "^#{boost()}" if boost() != 1.0
      return buffer
    end
  end
end
