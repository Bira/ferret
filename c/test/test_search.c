#include <ctype.h>
#include "search.h"
#include "array.h"
#include "helper.h"
#include "testhelper.h"
#include "test.h"

#define ARRAY_SIZE 40

static void test_byte_float_conversion(tst_case *tc, void *data)
{
    int i;
    (void)data;

    for (i = 0; i < 256; i++) {
        Aiequal(i, float2byte(byte2float((char)i)));
    }
}

static int my_doc_freq(Searcher *searcher, const char *field,
                       const char *term)
{
    (void)searcher; (void)field; (void)term;
    return 9;
}

static int my_max_doc(Searcher *searcher)
{
    (void)searcher;
    return 10;
}

static void test_explanation(tst_case *tc, void *data)
{
    Explanation *expl = expl_new(1.6f, "short description");
    char *str = expl_to_s(expl);
    (void)data;
    Asequal("1.6 = short description\n", str);
    free(str);
    expl_add_detail(expl, expl_new(0.8f, "half the score"));
    expl_add_detail(expl, expl_new(2.0f, "to make the difference"));
    expl_add_detail(expl->details[1], expl_new(0.5f, "sub-sub"));
    expl_add_detail(expl->details[1], expl_new(4.0f, "another sub-sub"));
    expl_add_detail(expl->details[0], expl_new(0.8f, "and sub-sub for 1st sub"));

    str = expl_to_s(expl);
    Asequal("1.6 = short description\n"
            "  0.8 = half the score\n"
            "    0.8 = and sub-sub for 1st sub\n"
            "  2.0 = to make the difference\n"
            "    0.5 = sub-sub\n"
            "    4.0 = another sub-sub\n", str);
    expl_destroy(expl);
    free(str);
}

static void test_default_similarity(tst_case *tc, void *data)
{
    PhrasePosition positions[4];
    Searcher searcher;
    Similarity *dsim = sim_create_default();
    (void)data;
    positions[0].pos = 0;
    positions[0].terms = ary_new_type(char *);
    ary_push(positions[0].terms, (char *)"term1");
    ary_push(positions[0].terms, (char *)"term2");
    ary_push(positions[0].terms, (char *)"term3");

    positions[1].pos = 0;
    positions[1].terms = ary_new_type(char *);
    ary_push(positions[0].terms, (char *)"term1");
    ary_push(positions[0].terms, (char *)"term2");

    positions[2].pos = -100;
    positions[2].terms = ary_new_type(char *);
    ary_push(positions[0].terms, (char *)"term1");

    positions[3].pos = 100;
    positions[3].terms = ary_new_type(char *);
    ary_push(positions[3].terms, (char *)"term1");
    ary_push(positions[3].terms, (char *)"term2");
    ary_push(positions[3].terms, (char *)"term2");
    ary_push(positions[3].terms, (char *)"term3");
    ary_push(positions[3].terms, (char *)"term4");
    ary_push(positions[3].terms, (char *)"term5");

    Afequal(1.0/4, sim_length_norm(dsim, "field", 16));
    Afequal(1.0/4, sim_query_norm(dsim, 16));
    Afequal(3.0, sim_tf(dsim, 9));
    Afequal(1.0/10, sim_sloppy_freq(dsim, 9));
    Afequal(1.0, sim_idf(dsim, 9, 10));
    Afequal(4.0, sim_coord(dsim, 12, 3));
    searcher.doc_freq = &my_doc_freq;
    searcher.max_doc = &my_max_doc;
    Afequal(1.0, sim_idf_term(dsim, "field", positions[0].terms[0], &searcher));
    Afequal(12.0, sim_idf_phrase(dsim, "field", positions, 4, &searcher));

    ary_free(positions[0].terms);
    ary_free(positions[1].terms);
    ary_free(positions[2].terms);
    ary_free(positions[3].terms);
}

typedef struct DoubleFilter {
    TokenFilter super;
    Token *tk;
} DoubleFilter;

static Token *dbl_tf_next(TokenStream *ts)
{
    Token *tk;
    tk = ((DoubleFilter *)ts)->tk;
    if (tk && islower(tk->text[0])) {
        char *t = tk->text;
        while (*t) {
            *t = toupper(*t);
            t++;
        }
        tk->pos_inc = 1;
    }
    else {
        tk = ((DoubleFilter *)ts)->tk
            = ((TokenFilter *)ts)->sub_ts->next(((TokenFilter *)ts)->sub_ts);
        if (tk && islower(tk->text[0])) {
            tk->pos_inc = 0;
        }
    }
    return tk;
}

static TokenStream *dbl_tf_clone_i(TokenStream *ts)
{
    return filter_clone_size(ts, sizeof(DoubleFilter));
}

static TokenStream *dbl_tf_new(TokenStream *sub_ts)
{
    TokenStream *ts = tf_new(DoubleFilter, sub_ts);
    ts->next        = &dbl_tf_next;
    ts->clone_i     = &dbl_tf_clone_i;
    return ts;
}

Analyzer *dbl_analyzer_new()
{
    TokenStream *ts;
    ts = dbl_tf_new(whitespace_tokenizer_new());
    return analyzer_new(ts, NULL, NULL);
}

struct Data {
    char *date;
    char *field;
    char *cat;
    char *number;
};

static const char *date = "date";
static const char *field = "field";
static const char *cat = "cat";
static const char *number = "number";

#define SEARCH_DOCS_SIZE 18
struct Data test_data[SEARCH_DOCS_SIZE] = {
    {"20050930", "word1",
        "cat1/",                ".123"},
    {"20051001", "word1 word2 the quick brown fox",
        "cat1/sub1",            "0.954"},
    {"20051002", "word1 word3",
        "cat1/sub1/subsub1",    "908.123434"},
    {"20051003", "word1 word3",
        "cat1/sub2",            "3999"},
    {"20051004", "word1 word2",
        "cat1/sub2/subsub2",    "+.3413"},
    {"20051005", "word1",
        "cat2/sub1",            "-1.1298"},
    {"20051006", "word1 word3",
        "cat2/sub1",            "2"},
    {"20051007", "word1",
        "cat2/sub1",            "+8.894"},
    {"20051008", "word1 word2 word3 the fast brown fox",
        "cat2/sub1",            "+84783.13747"},
    {"20051009", "word1",
        "cat3/sub1",            "10.0"},
    {"20051010", "word1",
        "cat3/sub1",            "1"},
    {"20051011", "word1 word3 the quick red fox",
        "cat3/sub1",            "-12518419"},
    {"20051012", "word1",
        "cat3/sub1",            "10"},
    {"20051013", "word1",
        "cat1/sub2",            "15682954"},
    {"20051014", "word1 word3 the quick hairy fox",
        "cat1/sub1",            "98132"},
    {"20051015", "word1",
        "cat1/sub2/subsub1",    "-.89321"},
    {"20051016", "word1 the quick fox is brown and hairy and a little red",
        "cat1/sub1/subsub2",    "-89"},
    {"20051017", "word1 the brown fox is quick and red",
        "cat1/",                "-1.0"}
};
static void prepare_search_index(Store *store)
{
    int i;
    IndexWriter *iw;

    FieldInfos *fis = fis_new(STORE_YES, INDEX_YES, TERM_VECTOR_YES);
    index_create(store, fis);
    fis_deref(fis);

    iw = iw_open(store, dbl_analyzer_new(), NULL);
    for (i = 0; i < SEARCH_DOCS_SIZE; i++) {
        Document *doc = doc_new();
        doc->boost = (float)(i+1);
        doc_add_field(doc, df_add_data(df_new(date), test_data[i].date));
        doc_add_field(doc, df_add_data(df_new(field), test_data[i].field));
        doc_add_field(doc, df_add_data(df_new(cat), test_data[i].cat));
        doc_add_field(doc, df_add_data(df_new(number), test_data[i].number));
        iw_add_doc(iw, doc);
        doc_destroy(doc);
    }
    iw_close(iw);
}

static void test_get_doc(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Document *doc;
    DocField *df;
    Aiequal(SEARCH_DOCS_SIZE, searcher_max_doc(searcher));

    doc = searcher_get_doc(searcher, 0);
    df = doc_get_field(doc, date);
    Aiequal(1, df->size);
    Asequal("20050930", df->data[0]);
    doc_destroy(doc);

    doc = searcher_get_doc(searcher, 4);
    df = doc_get_field(doc, cat);
    Aiequal(1, df->size);
    Asequal("cat1/sub2/subsub2", df->data[0]);
    doc_destroy(doc);

    doc = searcher_get_doc(searcher, 12);
    df = doc_get_field(doc, date);
    Aiequal(1, df->size);
    Asequal("20051012", df->data[0]);
    doc_destroy(doc);
}

void check_to_s(tst_case *tc, Query *query, const char *field, char *q_str)
{
    char *q_res = query->to_s(query, field);
    Asequal(q_str, q_res);
    /*
    printf("%s\n", q_res);
    */
    free(q_res);
}

void check_hits(tst_case *tc, Searcher *searcher, Query *query,
                char *expected_hits, char top)
{
    static int num_array[ARRAY_SIZE];
    static int num_array2[ARRAY_SIZE];
    int i, count;
    int total_hits = s2l(expected_hits, num_array);
    TopDocs *top_docs
        = searcher_search(searcher, query, 0, total_hits + 1, NULL, NULL, NULL);
    if (!Aiequal(total_hits, top_docs->total_hits)) {
        int i;
        Tmsg_nf("\texpected;\n\t    ");
        for (i = 0; i < total_hits; i++) {
            Tmsg_nf("%d ", num_array[i]);
        }
        Tmsg_nf("\n\tsaw;\n\t    ");
        for (i = 0; i < top_docs->size; i++) {
            Tmsg_nf("%d ", top_docs->hits[i]->doc);
        }
        Tmsg_nf("\n");
    }
    Aiequal(total_hits, top_docs->size);

    /* FIXME add this code once search_unscored is working
    searcher_search_unscored(searcher, query, buf, ARRAY_SIZE, 0);
    Aaiequal(num_array, buf, total_hits);
    */

    if ((top >= 0) && top_docs->size)
        Aiequal(top, top_docs->hits[0]->doc);

    /* printf("top_docs->size = %d\n", top_docs->size); */
    for (i = 0; i < top_docs->size; i++) {
        Hit *hit = top_docs->hits[i];
        float normalized_score = hit->score / top_docs->max_score;
        Assert(0.0 < normalized_score && normalized_score <= 1.0,
               "hit->score <%f> is out of range (0.0..1.0]", normalized_score);
        Assert(ary_includes(num_array, total_hits, hit->doc),
               "doc %d was found unexpectedly", hit->doc);
        /* only check the explanation if we got the correct docs. Obviously we
         * might want to remove this to visually check the explanations */
        if (total_hits == top_docs->total_hits) {
            Explanation *e = searcher_explain(searcher, query, hit->doc);
            if (! Afequal(hit->score, e->value)) {
               char *t;
               Tmsg("\n\"\"\"\n%d>>\n%f\n%s\n\"\"\"\n", hit->doc, hit->score,
                      t = expl_to_s(e));
               free(t);
            }
/*
char *t;
printf("\n\"\"\"\n%d>>\n%f\n%s\n\"\"\"\n", hit->doc, hit->score, t = expl_to_s(e));
free(t);
*/
            expl_destroy(e);
        }
    }
    td_destroy(top_docs);
    qsort(num_array, total_hits, sizeof(int), &icmp_risky);
    count = searcher_search_unscored(searcher, query,
                                     num_array2, ARRAY_SIZE, 0);
    Aaiequal(num_array, num_array2, total_hits);
    if (count > 3) {
        count = searcher_search_unscored(searcher, query,
                                         num_array2, ARRAY_SIZE, num_array2[3]);
        Aaiequal(num_array + 3, num_array2, count);
    }
}

static void test_term_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    TopDocs *top_docs;
    Query *tq = tq_new(field, "word2");
    check_to_s(tc, tq, (char *)field, "word2");
    check_to_s(tc, tq, "", "field:word2");
    tq->boost = 100;
    check_hits(tc, searcher, tq, "4, 8, 1", -1);
    check_to_s(tc, tq, (char *)field, "word2^100.0");
    check_to_s(tc, tq, "", "field:word2^100.0");
    q_deref(tq);

    tq = tq_new(field, "2342");
    check_hits(tc, searcher, tq, "", -1);
    q_deref(tq);

    tq = tq_new(field, "");
    check_hits(tc, searcher, tq, "", -1);
    q_deref(tq);

    tq = tq_new("not_a_field", "word2");
    check_hits(tc, searcher, tq, "", -1);
    q_deref(tq);

    tq = tq_new(field, "word1");
    top_docs = searcher_search(searcher, tq, 0, 10, NULL, NULL, NULL);
    Aiequal(SEARCH_DOCS_SIZE, top_docs->total_hits);
    Aiequal(10, top_docs->size);
    td_destroy(top_docs);

    top_docs = searcher_search(searcher, tq, 0, 20, NULL, NULL, NULL);
    Aiequal(SEARCH_DOCS_SIZE, top_docs->total_hits);
    Aiequal(SEARCH_DOCS_SIZE, top_docs->size);
    td_destroy(top_docs);

    top_docs = searcher_search(searcher, tq, 10, 20, NULL, NULL, NULL);
    Aiequal(SEARCH_DOCS_SIZE, top_docs->total_hits);
    Aiequal(SEARCH_DOCS_SIZE - 10, top_docs->size);
    td_destroy(top_docs);
    q_deref(tq);
}

static void test_term_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;
    q1 = tq_new("A", "a");

    q2 = tq_new("A", "a");
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    Assert(q_eq(q1, q1), "Queries are equal");
    q_deref(q2);

    q2 = tq_new("A", "b");
    Assert(q_hash(q1) != q_hash(q2), "texts differ");
    Assert(!q_eq(q1, q2), "texts differ");
    q_deref(q2);

    q2 = tq_new("B", "a");
    Assert(q_hash(q1) != q_hash(q2), "fields differ");
    Assert(!q_eq(q1, q2), "fields differ");
    q_deref(q2);

    q_deref(q1);
}

static void test_boolean_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *bq = bq_new(false);
    Query *tq1 = tq_new(field, "word1");
    Query *tq2 = tq_new(field, "word3");
    Query *tq3 = tq_new(field, "word2");
    bq_add_query_nr(bq, tq1, BC_MUST);
    bq_add_query_nr(bq, tq2, BC_MUST);
    check_hits(tc, searcher, bq, "2, 3, 6, 8, 11, 14", 14);

    bq_add_query_nr(bq, tq3, BC_SHOULD);
    check_hits(tc, searcher, bq, "2, 3, 6, 8, 11, 14", 8);
    q_deref(bq);

    tq2 = tq_new(field, "word3");
    tq3 = tq_new(field, "word2");
    bq = bq_new(false);
    bq_add_query_nr(bq, tq2, BC_MUST);
    bq_add_query_nr(bq, tq3, BC_MUST_NOT);
    check_hits(tc, searcher, bq, "2, 3, 6, 11, 14", -1);
    q_deref(bq);

    tq2 = tq_new(field, "word3");
    bq = bq_new(false);
    bq_add_query_nr(bq, tq2, BC_MUST_NOT);
    check_hits(tc, searcher, bq, "0,1,4,5,7,9,10,12,13,15,16,17", -1);
    q_deref(bq);
    return;

    tq2 = tq_new(field, "word3");
    bq = bq_new(false);
    bq_add_query_nr(bq, tq2, BC_SHOULD);
    check_hits(tc, searcher, bq, "2, 3, 6, 8, 11, 14", 14);
    q_deref(bq);

    tq2 = tq_new(field, "word3");
    tq3 = tq_new(field, "word2");
    bq = bq_new(false);
    bq_add_query_nr(bq, tq2, BC_SHOULD);
    bq_add_query_nr(bq, tq3, BC_SHOULD);
    check_hits(tc, searcher, bq, "1, 2, 3, 4, 6, 8, 11, 14", -1);
    q_deref(bq);

    bq = bq_new(false);
    tq1 = tq_new("not a field", "word1");
    tq2 = tq_new("not a field", "word3");
    tq3 = tq_new(field, "word2");
    bq_add_query_nr(bq, tq1, BC_SHOULD);
    bq_add_query_nr(bq, tq2, BC_SHOULD);
    check_hits(tc, searcher, bq, "", -1);

    bq_add_query_nr(bq, tq3, BC_SHOULD);
    check_hits(tc, searcher, bq, "1, 4, 8", 4);

    q_deref(bq);
}

static void test_boolean_query_hash(tst_case *tc, void *data)
{
    Query *tq1, *tq2, *tq3, *q1, *q2;
    (void)data;

    tq1 = tq_new("A", "1");
    tq2 = tq_new("B", "2");
    tq3 = tq_new("C", "3");
    q1 = bq_new(false);
    bq_add_query(q1, tq1, BC_MUST);
    bq_add_query(q1, tq2, BC_MUST);

    q2 = bq_new(false);
    bq_add_query(q2, tq1, BC_MUST);
    bq_add_query(q2, tq2, BC_MUST);

    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q1), "Queries are equal");
    Assert(q_eq(q1, q2), "Queries are equal");
    Assert(q_hash(q1) != q_hash(tq1), "Queries are not equal");
    Assert(!q_eq(q1, tq1), "Queries are not equal");
    Assert(!q_eq(tq1, q1), "Queries are not equal");
    q_deref(q2);

    q2 = bq_new(true);
    bq_add_query(q2, tq1, BC_MUST);
    bq_add_query(q2, tq2, BC_MUST);

    Assert(q_hash(q1) != q_hash(q2), "Queries are not equal");
    Assert(!q_eq(q1, q2), "Queries are not equal");
    q_deref(q2);

    q2 = bq_new(false);
    bq_add_query(q2, tq1, BC_SHOULD);
    bq_add_query(q2, tq2, BC_MUST_NOT);

    Assert(q_hash(q1) != q_hash(q2), "Queries are not equal");
    Assert(!q_eq(q1, q2), "Queries are not equal");
    q_deref(q2);

    q2 = bq_new(false);
    bq_add_query(q2, tq1, BC_MUST);
    bq_add_query(q2, tq2, BC_MUST);
    bq_add_query(q2, tq3, BC_MUST);

    Assert(q_hash(q1) != q_hash(q2), "Queries are not equal");
    Assert(!q_eq(q1, q2), "Queries are not equal");

    bq_add_query(q1, tq3, BC_MUST);

    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);

    q_deref(q1);
    q_deref(tq1);
    q_deref(tq2);
    q_deref(tq3);
}

static void test_phrase_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *q;
    Query *phq = phq_new(field);
    check_to_s(tc, phq, field, "\"\"");
    check_to_s(tc, phq, "", "field:\"\"");

    phq_add_term(phq, "quick", 1);
    phq_add_term(phq, "brown", 1);
    phq_add_term(phq, "fox", 1);
    check_to_s(tc, phq, field, "\"quick brown fox\"");
    check_to_s(tc, phq, "", "field:\"quick brown fox\"");
    check_hits(tc, searcher, phq, "1", 1);

    ((PhraseQuery *)phq)->slop = 4;
    check_hits(tc, searcher, phq, "1, 16, 17", 17);
    q_deref(phq);

    phq = phq_new(field);
    phq_add_term(phq, "quick", 1);
    phq_add_term(phq, "fox", 2);
    check_to_s(tc, phq, field, "\"quick <> fox\"");
    check_to_s(tc, phq, "", "field:\"quick <> fox\"");
    check_hits(tc, searcher, phq, "1, 11, 14", 14);

    ((PhraseQuery *)phq)->slop = 1;
    check_hits(tc, searcher, phq, "1, 11, 14, 16", 14);

    ((PhraseQuery *)phq)->slop = 4;
    check_hits(tc, searcher, phq, "1, 11, 14, 16, 17", 14);
    phq_add_term(phq, "red", -1);
    check_to_s(tc, phq, "", "field:\"quick red fox\"~4");
    check_hits(tc, searcher, phq, "11", 11);
    phq_add_term(phq, "RED", 0);
    check_to_s(tc, phq, "", "field:\"quick red RED&fox\"~4");
    check_hits(tc, searcher, phq, "11", 11);
    phq_add_term(phq, "QUICK", -1);
    phq_add_term(phq, "red", 0);
    check_to_s(tc, phq, "", "field:\"quick QUICK&red&red RED&fox\"~4");
    check_hits(tc, searcher, phq, "11", 11);
    phq_add_term(phq, "green", 0);
    phq_add_term(phq, "yellow", 0);
    check_to_s(tc, phq, "",
               "field:\"quick QUICK&red&red RED&fox&green&yellow\"~4");
    q_deref(phq);

    phq = phq_new(field);
    phq_add_term(phq, "the", 0);
    phq_add_term(phq, "WORD3", 0);
    check_hits(tc, searcher, phq, "8, 11, 14", 14);
    phq_add_term(phq, "THE", 1);
    phq_add_term(phq, "quick", 0);
    phq_add_term(phq, "QUICK", 1);
    check_hits(tc, searcher, phq, "11, 14", 14);
    check_to_s(tc, phq, "", "field:\"WORD3&the THE&quick QUICK\"");
    q_deref(phq);

    phq = phq_new("not a field");
    phq_add_term(phq, "the", 0);
    phq_add_term(phq, "quick", 1);
    check_hits(tc, searcher, phq, "", -1);
    q_deref(phq);

    /* test single term case, query is rewritten to TermQuery */
    phq = phq_new(field);
    phq_add_term(phq, "word2", 1);
    check_hits(tc, searcher, phq, "4, 8, 1", -1);
    q = searcher_rewrite(searcher, phq);
    Aiequal(q->type, TERM_QUERY);
    q_deref(phq);
    q_deref(q);
}

static void test_phrase_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;

    q1 = phq_new(field);
    phq_add_term(q1, "quick", 1);
    phq_add_term(q1, "brown", 2);
    phq_add_term(q1, "fox", 0);

    q2 = phq_new(field);
    phq_add_term(q2, "quick", 1);
    phq_add_term(q2, "brown", 2);
    phq_add_term(q2, "fox", 0);

    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q1), "Test query equals itself");
    Assert(q_eq(q1, q2), "Queries should be equal");

    ((PhraseQuery *)q2)->slop = 5;
    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");
    q_deref(q2);

    q2 = phq_new(field);
    phq_add_term(q2, "quick", 1);
    phq_add_term(q2, "brown", 1);
    phq_add_term(q2, "fox", 1);

    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");
    q_deref(q2);

    q2 = phq_new(field);
    phq_add_term(q2, "fox", 1);
    phq_add_term(q2, "brown", 2);
    phq_add_term(q2, "quick", 0);

    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");
    q_deref(q2);

    q2 = phq_new("other_field");
    phq_add_term(q2, "quick", 1);
    phq_add_term(q2, "brown", 2);
    phq_add_term(q2, "fox", 0);

    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");
    q_deref(q2);
    q_deref(q1);
}

static void test_multi_phrase_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *phq, *q;

    phq = phq_new(field);
    phq_add_term(phq, "quick", 0);
    phq_append_multi_term(phq, "fast");
    check_hits(tc, searcher, phq, "1, 8, 11, 14, 16, 17", -1);
    check_to_s(tc, phq, field, "\"quick|fast\"");
    check_to_s(tc, phq, "", "field:\"quick|fast\"");

    phq_add_term(phq, "brown", 1);
    phq_append_multi_term(phq, "red");
    phq_append_multi_term(phq, "hairy");
    phq_add_term(phq, "fox", 1);
    check_to_s(tc, phq, field, "\"quick|fast brown|red|hairy fox\"");
    check_to_s(tc, phq, "", "field:\"quick|fast brown|red|hairy fox\"");
    check_hits(tc, searcher, phq, "1, 8, 11, 14", -1);

    ((PhraseQuery *)phq)->slop = 4;
    check_hits(tc, searcher, phq, "1, 8, 11, 14, 16, 17", -1);
    check_to_s(tc, phq, "", "field:\"quick|fast brown|red|hairy fox\"~4");

    phq_add_term(phq, "QUICK", -1);
    phq_append_multi_term(phq, "FAST");
    check_hits(tc, searcher, phq, "1, 8, 11, 14, 16, 17", -1);
    check_to_s(tc, phq, "",
               "field:\"quick|fast QUICK|FAST&brown|red|hairy fox\"~4");

    phq_add_term(phq, "WORD3", -3);
    phq_append_multi_term(phq, "WORD2");
    check_hits(tc, searcher, phq, "1, 8, 11, 14", -1);
    check_to_s(tc, phq, "", "field:\"WORD3|WORD2 quick|fast "
               "QUICK|FAST&brown|red|hairy fox\"~4");
    q_deref(phq);

    phq = phq_new("not a field");
    phq_add_term(phq, "the", 0);
    phq_add_term(phq, "quick", 1);
    phq_append_multi_term(phq, "THE");
    check_hits(tc, searcher, phq, "", -1);
    q_deref(phq);

    phq = phq_new(field);
    phq_add_term(phq, "word2", 1);
    phq_append_multi_term(phq, "word3");
    check_hits(tc, searcher, phq, "1, 2, 3, 4, 6, 8, 11, 14", -1);
    q = searcher_rewrite(searcher, phq);
    Aiequal(q->type, MULTI_TERM_QUERY);
    q_deref(phq);
    q_deref(q);

}

static void test_multi_phrase_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;

    q1 = phq_new(field);
    phq_add_term(q1, "quick", 1);
    phq_append_multi_term(q1, "fast");
    phq_add_term(q1, "brown", 1);
    phq_append_multi_term(q1, "red");
    phq_append_multi_term(q1, "hairy");
    phq_add_term(q1, "fox", 1);

    q2 = phq_new(field);
    phq_add_term(q2, "quick", 1);
    phq_append_multi_term(q2, "fast");
    phq_add_term(q2, "brown", 1);
    phq_append_multi_term(q2, "red");
    phq_append_multi_term(q2, "hairy");
    phq_add_term(q2, "fox", 1);

    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q1), "Test query equals itself");
    Assert(q_eq(q1, q2), "Queries should be equal");

    ((PhraseQuery *)q2)->slop = 5;
    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");

    phq_append_multi_term(q2, "hairy");
    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");
    q_deref(q2);

    /* test same but different order */
    q2 = phq_new(field);
    phq_add_term(q2, "quick", 1);
    phq_append_multi_term(q2, "fast");
    phq_add_term(q2, "fox", 1);
    phq_add_term(q2, "brown", 1);
    phq_append_multi_term(q2, "red");
    phq_append_multi_term(q2, "hairy");

    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");
    q_deref(q2);

    /* test same but different pos values */
    q2 = phq_new(field);
    phq_add_term(q2, "quick", 1);
    phq_append_multi_term(q2, "fast");
    phq_add_term(q2, "brown", 1);
    phq_append_multi_term(q2, "red");
    phq_append_multi_term(q2, "hairy");
    phq_add_term(q2, "fox", 2);

    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");
    q_deref(q2);

    q_deref(q1);
}

static void test_multi_term_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *mtq = multi_tq_new_conf(field, 4, 0.5);
    check_hits(tc, searcher, mtq, "", -1);
    check_to_s(tc, mtq, field, "\"\"");
    check_to_s(tc, mtq, "", "field:\"\"");

    multi_tq_add_term(mtq, "brown");
    check_hits(tc, searcher, mtq, "1, 8, 16, 17", -1);
    check_to_s(tc, mtq, field, "\"brown\"");
    check_to_s(tc, mtq, "", "field:\"brown\"");

    multi_tq_add_term_boost(mtq, "fox", 0.1f);
    check_hits(tc, searcher, mtq, "1, 8, 16, 17", -1);
    check_to_s(tc, mtq, field, "\"brown\"");
    check_to_s(tc, mtq, "", "field:\"brown\"");

    multi_tq_add_term_boost(mtq, "fox", 0.6f);
    check_hits(tc, searcher, mtq, "1, 8, 11, 14, 16, 17", -1);
    check_to_s(tc, mtq, field, "\"fox^0.6|brown\"");
    check_to_s(tc, mtq, "", "field:\"fox^0.6|brown\"");

    multi_tq_add_term_boost(mtq, "fast", 50.0f);
    check_hits(tc, searcher, mtq, "1, 8, 11, 14, 16, 17", 8);
    check_to_s(tc, mtq, field, "\"fox^0.6|brown|fast^50.0\"");
    check_to_s(tc, mtq, "", "field:\"fox^0.6|brown|fast^50.0\"");


    mtq->boost = 80.1f;
    check_to_s(tc, mtq, "", "field:\"fox^0.6|brown|fast^50.0\"^80.1");
    multi_tq_add_term(mtq, "word1");
    check_to_s(tc, mtq, "", "field:\"fox^0.6|brown|word1|fast^50.0\"^80.1");
    multi_tq_add_term(mtq, "word2");
    check_to_s(tc, mtq, "", "field:\"brown|word1|word2|fast^50.0\"^80.1");
    multi_tq_add_term(mtq, "word3");
    check_to_s(tc, mtq, "", "field:\"brown|word1|word2|fast^50.0\"^80.1");
/*
char *t;
Explanation *e = searcher_explain(searcher, mtq, 8);
printf("\n\"\"\"\n%s\n\"\"\"\n", t = expl_to_s(e));
free(t);
*/
    q_deref(mtq);
}

static void test_multi_term_query_hash(tst_case *tc, void *data)
{
    Query *q1 = multi_tq_new_conf(field, 100, 0.4);
    Query *q2 = multi_tq_new(field);
    (void)data;


    check_to_s(tc, q1, "", "field:\"\"");
    Assert(q_hash(q1) == q_hash(q2), "Queries should be equal");
    Assert(q_eq(q1, q1), "Same queries should be equal");
    Assert(q_eq(q1, q2), "Queries should be equal");

    multi_tq_add_term(q1, "word1");
    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");

    multi_tq_add_term(q2, "word1");
    Assert(q_hash(q1) == q_hash(q2), "Queries should be equal");
    Assert(q_eq(q1, q2), "Queries should be equal");

    multi_tq_add_term(q1, "word2");
    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");

    multi_tq_add_term_boost(q2, "word2", 1.5);
    Assert(q_hash(q1) != q_hash(q2), "Queries should not be equal");
    Assert(!q_eq(q1, q2), "Queries should not be equal");

    q_deref(q1);
    q_deref(q2);
}

static void test_prefix_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *prq = prefixq_new(cat, "cat1");
    check_to_s(tc, prq, cat, "cat1*");
    check_hits(tc, searcher, prq, "0, 1, 2, 3, 4, 13, 14, 15, 16, 17", -1);
    q_deref(prq);

    prq = prefixq_new(cat, "cat1/sub2");
    check_to_s(tc, prq, cat, "cat1/sub2*");
    check_hits(tc, searcher, prq, "3, 4, 13, 15", -1);
    q_deref(prq);

    prq = prefixq_new(cat, "cat1/sub");
    check_to_s(tc, prq, cat, "cat1/sub*");
    check_hits(tc, searcher, prq, "1, 2, 3, 4, 13, 14, 15, 16", -1);
    q_deref(prq);

    prq = prefixq_new("unknown field", "cat1/sub");
    check_to_s(tc, prq, cat, "unknown field:cat1/sub*");
    check_hits(tc, searcher, prq, "", -1);
    q_deref(prq);

    prq = prefixq_new(cat, "unknown_term");
    check_to_s(tc, prq, cat, "unknown_term*");
    check_hits(tc, searcher, prq, "", -1);
    q_deref(prq);
}

static void test_prefix_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;
    q1 = prefixq_new("A", "a");

    q2 = prefixq_new("A", "a");
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "TermQueries are equal");
    Assert(q_eq(q1, q1), "TermQueries are same");
    q_deref(q2);

    q2 = prefixq_new("A", "b");
    Assert(q_hash(q1) != q_hash(q2), "TermQueries are not equal");
    Assert(!q_eq(q1, q2), "TermQueries are not equal");
    q_deref(q2);

    q2 = prefixq_new("B", "a");
    Assert(q_hash(q1) != q_hash(q2), "TermQueries are not equal");
    Assert(!q_eq(q1, q2), "TermQueries are not equal");
    q_deref(q2);

    q_deref(q1);
}

static void test_range_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *rq;
    rq = rq_new(date, "20051006", "20051010", true, true);
    check_hits(tc, searcher, rq, "6,7,8,9,10", -1);
    q_deref(rq);

    rq = rq_new(date, "20051006", "20051010", false, true);
    check_hits(tc, searcher, rq, "7,8,9,10", -1);
    q_deref(rq);

    rq = rq_new(date, "20051006", "20051010", true, false);
    check_hits(tc, searcher, rq, "6,7,8,9", -1);
    q_deref(rq);

    rq = rq_new(date, "20051006", "20051010", false, false);
    check_hits(tc, searcher, rq, "7,8,9", -1);
    q_deref(rq);

    rq = rq_new(date, NULL, "20051003", false, true);
    check_hits(tc, searcher, rq, "0,1,2,3", -1);
    q_deref(rq);

    rq = rq_new(date, NULL, "20051003", false, false);
    check_hits(tc, searcher, rq, "0,1,2", -1);
    q_deref(rq);

    rq = rq_new_less(date, "20051003", true);
    check_hits(tc, searcher, rq, "0,1,2,3", -1);
    q_deref(rq);

    rq = rq_new_less(date, "20051003", false);
    check_hits(tc, searcher, rq, "0,1,2", -1);
    q_deref(rq);

    rq = rq_new(date, "20051014", NULL, true, false);
    check_hits(tc, searcher, rq, "14,15,16,17", -1);
    q_deref(rq);

    rq = rq_new(date, "20051014", NULL, false, false);
    check_hits(tc, searcher, rq, "15,16,17", -1);
    q_deref(rq);

    rq = rq_new_more(date, "20051014", true);
    check_hits(tc, searcher, rq, "14,15,16,17", -1);
    q_deref(rq);

    rq = rq_new_more(date, "20051014", false);
    check_hits(tc, searcher, rq, "15,16,17", -1);
    q_deref(rq);

    rq = rq_new("not_a_field", "20051006", "20051010", false, false);
    check_hits(tc, searcher, rq, "", -1);
    q_deref(rq);

    /* below range - no results */
    rq = rq_new(date, "10051006", "10051010", false, false);
    check_hits(tc, searcher, rq, "", -1);
    q_deref(rq);

    /* above range - no results */
    rq = rq_new(date, "30051006", "30051010", false, false);
    check_hits(tc, searcher, rq, "", -1);
    q_deref(rq);
}

static void test_range_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;
    q1 = rq_new(date, "20051006", "20051010", true, true);
    q2 = rq_new(date, "20051006", "20051010", true, true);

    Assert(q_eq(q1, q1), "Test same queries are equal");
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);

    q2 = rq_new(date, "20051006", "20051010", true, false);
    Assert(q_hash(q1) != q_hash(q2), "Upper bound include differs");
    Assert(!q_eq(q1, q2), "Upper bound include differs");
    q_deref(q2);

    q2 = rq_new(date, "20051006", "20051010", false, true);
    Assert(q_hash(q1) != q_hash(q2), "Lower bound include differs");
    Assert(!q_eq(q1, q2), "Lower bound include differs");
    q_deref(q2);

    q2 = rq_new(date, "20051006", "20051011", true, true);
    Assert(q_hash(q1) != q_hash(q2), "Upper bound differs");
    Assert(!q_eq(q1, q2), "Upper bound differs");
    q_deref(q2);

    q2 = rq_new(date, "20051005", "20051010", true, true);
    Assert(q_hash(q1) != q_hash(q2), "Lower bound differs");
    Assert(!q_eq(q1, q2), "Lower bound differs");
    q_deref(q2);

    q2 = rq_new(date, "20051006", NULL, true, false);
    Assert(q_hash(q1) != q_hash(q2), "Upper bound is NULL");
    Assert(!q_eq(q1, q2), "Upper bound is NULL");
    q_deref(q2);

    q2 = rq_new(date, NULL, "20051010", false, true);
    Assert(q_hash(q1) != q_hash(q2), "Lower bound is NULL");
    Assert(!q_eq(q1, q2), "Lower bound is NULL");
    q_deref(q2);

    q2 = rq_new(field, "20051006", "20051010", true, true);
    Assert(q_hash(q1) != q_hash(q2), "Field differs");
    Assert(!q_eq(q1, q2), "Field differs");
    q_deref(q2);
    q_deref(q1);

    q1 = rq_new(date, NULL, "20051010", false, true);
    q2 = rq_new(date, NULL, "20051010", false, true);
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);
    q_deref(q1);

    q1 = rq_new(date, "20051010", NULL, true, false);
    q2 = rq_new(date, "20051010", NULL, true, false);
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);
    q_deref(q1);
}

static void test_typed_range_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *trq;
    trq = trq_new(number, "-1.0", "1.0", true, true);
    check_hits(tc, searcher, trq, "0,1,4,10,15,17", -1);
    q_deref(trq);

    trq = trq_new(number, "-1.0", "1.0", false, false);
    check_hits(tc, searcher, trq, "0,1,4,15", -1);
    q_deref(trq);

    /* text hexadecimal */
    trq = trq_new(number, "1.0", "10", false, true);
    check_hits(tc, searcher, trq, "6,7,9,12", -1);
    q_deref(trq);

    /* test single bound */
    trq = trq_new(number, NULL, "0", false, true);
    check_hits(tc, searcher, trq, "5,11,15,16,17", -1);
    q_deref(trq);

    /* test single bound */
    trq = trq_new(number, "0", NULL, false, false);
    check_hits(tc, searcher, trq, "0,1,2,3,4,6,7,8,9,10,12,13,14", -1);
    q_deref(trq);

    /* below range - no results */
    trq = trq_new(number, "10051006", "10051010", false, false);
    check_hits(tc, searcher, trq, "", -1);
    q_deref(trq);

    /* above range - no results */
    trq = trq_new(number, "-12518421", "-12518420", true, true);
    check_hits(tc, searcher, trq, "", -1);
    q_deref(trq);

    /* should be normal range query for string fields */
    trq = trq_new(cat, "cat2", NULL, true, false);
    check_hits(tc, searcher, trq, "5,6,7,8,9,10,11,12", -1);
    q_deref(trq);
}

static void test_typed_range_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;
    q1 = trq_new(date, "20051006", "20051010", true, true);
    q2 = trq_new(date, "20051006", "20051010", true, true);

    Assert(q_eq(q1, q1), "Test same queries are equal");
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);

    q2 = trq_new(date, "20051006", "20051010", true, false);
    Assert(q_hash(q1) != q_hash(q2), "Upper bound include differs");
    Assert(!q_eq(q1, q2), "Upper bound include differs");
    q_deref(q2);

    q2 = trq_new(date, "20051006", "20051010", false, true);
    Assert(q_hash(q1) != q_hash(q2), "Lower bound include differs");
    Assert(!q_eq(q1, q2), "Lower bound include differs");
    q_deref(q2);

    q2 = trq_new(date, "20051006", "20051011", true, true);
    Assert(q_hash(q1) != q_hash(q2), "Upper bound differs");
    Assert(!q_eq(q1, q2), "Upper bound differs");
    q_deref(q2);

    q2 = trq_new(date, "20051005", "20051010", true, true);
    Assert(q_hash(q1) != q_hash(q2), "Lower bound differs");
    Assert(!q_eq(q1, q2), "Lower bound differs");
    q_deref(q2);

    q2 = trq_new(date, "20051006", NULL, true, false);
    Assert(q_hash(q1) != q_hash(q2), "Upper bound is NULL");
    Assert(!q_eq(q1, q2), "Upper bound is NULL");
    q_deref(q2);

    q2 = trq_new(date, NULL, "20051010", false, true);
    Assert(q_hash(q1) != q_hash(q2), "Lower bound is NULL");
    Assert(!q_eq(q1, q2), "Lower bound is NULL");
    q_deref(q2);

    q2 = trq_new(field, "20051006", "20051010", true, true);
    Assert(q_hash(q1) != q_hash(q2), "Field differs");
    Assert(!q_eq(q1, q2), "Field differs");
    q_deref(q2);
    q_deref(q1);

    q1 = trq_new(date, NULL, "20051010", false, true);
    q2 = trq_new(date, NULL, "20051010", false, true);
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);
    q_deref(q1);

    q1 = trq_new(date, "20051010", NULL, true, false);
    q2 = trq_new(date, "20051010", NULL, true, false);
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);
    q_deref(q1);

    q1 = trq_new(date, "20051010", NULL, true, false);
    q2 = rq_new(date, "20051010", NULL, true, false);
    Assert(q_hash(q1) != q_hash(q2), "TypedRangeQuery is not RangeQuery");
    Assert(!q_eq(q1, q2), "Queries are not equal");
    q_deref(q2);
    q_deref(q1);
}

static void test_wildcard_match(tst_case *tc, void *data)
{
    (void)data;
    (void)tc;
    Assert(!wc_match("", "abc"), "Empty pattern matches nothing");
    Assert(wc_match("*", "asdasdg"), "Star matches everything");
    Assert(wc_match("asd*", "asdasdg"), "Star matches everything after");
    Assert(wc_match("*dg", "asdasdg"), "Star matches everything before");
    Assert(wc_match("a?d*", "asdasdg"), "Q-mark matchs one char");
    Assert(wc_match("?sd*", "asdasdg"), "Q-mark can come first");
    Assert(wc_match("asd?", "asdg"), "Q-mark can come last");
    Assert(wc_match("asdg", "asdg"), "No special chars");
    Assert(!wc_match("asdf", "asdi"), "Do not match");
    Assert(!wc_match("asd??", "asdg"), "Q-mark must match");
    Assert(wc_match("as?g", "asdg"), "Q-mark matches in");
    Assert(!wc_match("as??g", "asdg"), "Q-mark must match");
    Assert(wc_match("a*?f", "asdf"), "Q-mark and star can appear together");
    Assert(wc_match("a?*f", "asdf"), "Q-mark and star can appear together");
    Assert(wc_match("a*?df", "asdf"), "Q-mark and star can appear together");
    Assert(wc_match("a?*df", "asdf"), "Q-mark and star can appear together");
    Assert(!wc_match("as*?df", "asdf"), "Q-mark must match");
    Assert(!wc_match("as?*df", "asdf"), "Q-mark must match");
    Assert(wc_match("asdf*", "asdf"), "Star can match nothing");
    Assert(wc_match("asd*f", "asdf"), "Star can match nothing");
    Assert(wc_match("*asdf*", "asdf"), "Star can match nothing");
    Assert(wc_match("asd?*****", "asdf"), "Can have multiple stars");
    Assert(wc_match("as?*****g", "asdg"), "Can have multiple stars");
    Assert(!wc_match("*asdf", "asdi"), "Do not match");
    Assert(!wc_match("asdf*", "asdi"), "Do not match");
    Assert(!wc_match("*asdf*", "asdi"), "Do not match");
    Assert(!wc_match("cat1*", "cat2/sub1"), "Do not match");
}

static void test_wildcard_query(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    Query *wq = wcq_new(cat, "cat1*"), *bq;
    check_hits(tc, searcher, wq, "0, 1, 2, 3, 4, 13, 14, 15, 16, 17", -1);
    q_deref(wq);

    wq = wcq_new(cat, "cat1*/s*sub2");
    check_hits(tc, searcher, wq, "4, 16", -1);
    q_deref(wq);

    wq = wcq_new(cat, "cat1/sub?/su??ub2");
    check_hits(tc, searcher, wq, "4, 16", -1);
    q_deref(wq);

    wq = wcq_new(cat, "cat1/");
    check_hits(tc, searcher, wq, "0, 17", -1);
    q_deref(wq);

    wq = wcq_new("unknown_field", "cat1/");
    check_hits(tc, searcher, wq, "", -1);
    q_deref(wq);

    wq = wcq_new(cat, "unknown_term");
    check_hits(tc, searcher, wq, "", -1);
    q_deref(wq);

    bq = bq_new(false);
    bq_add_query_nr(bq, tq_new(field, "word1"), BC_MUST);
    wq = wcq_new(cat, "cat1*");
    check_hits(tc, searcher, wq, "0, 1, 2, 3, 4, 13, 14, 15, 16, 17", -1);

    bq_add_query_nr(bq, wq, BC_MUST);
    check_hits(tc, searcher, bq, "0, 1, 2, 3, 4, 13, 14, 15, 16, 17", -1);

    q_deref(bq);
}

static void test_wildcard_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;
    q1 = wcq_new("A", "a*");

    q2 = wcq_new("A", "a*");
    Assert(q_eq(q1, q1), "Test same queries are equal");
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);

    q2 = wcq_new("A", "a?");
    Assert(q_hash(q1) != q_hash(q2), "Queries are not equal");
    Assert(!q_eq(q1, q2), "Queries are not equal");
    q_deref(q2);

    q2 = wcq_new("B", "a?");
    Assert(q_hash(q1) != q_hash(q2), "Queries are not equal");
    Assert(!q_eq(q1, q2), "Queries are not equal");
    q_deref(q2);

    q_deref(q1);
}

static void test_match_all_query_hash(tst_case *tc, void *data)
{
    Query *q1, *q2;
    (void)data;
    q1 = maq_new();
    q2 = maq_new();

    Assert(q_eq(q1, q1), "Test same queries are equal");
    Aiequal(q_hash(q1), q_hash(q2));
    Assert(q_eq(q1, q2), "Queries are equal");
    q_deref(q2);

    q2 = wcq_new("A", "a*");
    Assert(q_hash(q1) != q_hash(q2), "Queries are not equal");
    Assert(!q_eq(q1, q2), "Queries are not equal");
    q_deref(q2);

    q_deref(q1);
}

static void test_search_unscored(tst_case *tc, void *data)
{
    Searcher *searcher = (Searcher *)data;
    int buf[5], expected[5], count;
    Query *tq = tq_new(field, "word1");
    count = searcher_search_unscored(searcher, tq, buf, 5, 0);
    Aiequal(s2l("0, 1, 2, 3, 4", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 5, 1);
    Aiequal(s2l("1, 2, 3, 4, 5", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 5, 12);
    Aiequal(s2l("12, 13, 14, 15, 16", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 5, 15);
    Aiequal(s2l("15, 16, 17", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 5, 16);
    Aiequal(s2l("16, 17", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 5, 17);
    Aiequal(s2l("17", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 5, 18);
    Aiequal(s2l("", expected), count);
    Aaiequal(expected, buf, count);
    q_deref(tq);

    tq = tq_new(field, "word3");
    count = searcher_search_unscored(searcher, tq, buf, 3, 0);
    Aiequal(s2l("2, 3, 6", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 3, 7);
    Aiequal(s2l("8, 11, 14", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 3, 6);
    Aiequal(s2l("6, 8, 11", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 3, 11);
    Aiequal(s2l("11, 14", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 3, 14);
    Aiequal(s2l("14", expected), count);
    Aaiequal(expected, buf, count);
    count = searcher_search_unscored(searcher, tq, buf, 3, 15);
    Aiequal(s2l("", expected), count);
    Aaiequal(expected, buf, count);
    q_deref(tq);
}

tst_suite *ts_search(tst_suite *suite)
{
    Store *store = open_ram_store();
    IndexReader *ir;
    Searcher *searcher;

    suite = ADD_SUITE(suite);

    tst_run_test(suite, test_explanation, NULL);
    tst_run_test(suite, test_byte_float_conversion, NULL);
    tst_run_test(suite, test_default_similarity, NULL);

    prepare_search_index(store);
    ir = ir_open(store);
    searcher = isea_new(ir);

    tst_run_test(suite, test_get_doc, (void *)searcher);

    tst_run_test(suite, test_term_query, (void *)searcher);
    tst_run_test(suite, test_term_query_hash, NULL);

    tst_run_test(suite, test_boolean_query, (void *)searcher);
    tst_run_test(suite, test_boolean_query_hash, NULL);

    tst_run_test(suite, test_phrase_query, (void *)searcher);
    tst_run_test(suite, test_phrase_query_hash, NULL);

    tst_run_test(suite, test_multi_phrase_query, (void *)searcher);
    tst_run_test(suite, test_multi_phrase_query_hash, NULL);

    tst_run_test(suite, test_multi_term_query, (void *)searcher);
    tst_run_test(suite, test_multi_term_query_hash, NULL);

    tst_run_test(suite, test_prefix_query, (void *)searcher);
    tst_run_test(suite, test_prefix_query_hash, NULL);

    tst_run_test(suite, test_range_query, (void *)searcher);
    tst_run_test(suite, test_range_query_hash, NULL);

    tst_run_test(suite, test_typed_range_query, (void *)searcher);
    tst_run_test(suite, test_typed_range_query_hash, NULL);

    tst_run_test(suite, test_wildcard_match, (void *)searcher);
    tst_run_test(suite, test_wildcard_query, (void *)searcher);
    tst_run_test(suite, test_wildcard_query_hash, NULL);

    tst_run_test(suite, test_match_all_query_hash, NULL);

    tst_run_test(suite, test_search_unscored, (void *)searcher);

    store_deref(store);
    searcher_close(searcher);
    return suite;
}



static void prepare_multi_search_index(Store *store, struct Data data[],
                                       int d_cnt, int w)
{
    int i;
    IndexWriter *iw;
    FieldInfos *fis = fis_new(STORE_YES, INDEX_YES, TERM_VECTOR_YES);
    index_create(store, fis);
    fis_deref(fis);

    iw = iw_open(store, dbl_analyzer_new(), NULL);
    for (i = 0; i < d_cnt; i++) {
        Document *doc = doc_new();
        doc->boost = (float)(i+w);
        doc_add_field(doc, df_add_data(df_new(date), data[i].date));
        doc_add_field(doc, df_add_data(df_new(field), data[i].field));
        doc_add_field(doc, df_add_data(df_new(cat), data[i].cat));
        doc_add_field(doc, df_add_data(df_new(number), data[i].number));
        iw_add_doc(iw, doc);
        doc_destroy(doc);
    }
    iw_close(iw);
}

static void test_query_combine(tst_case *tc, void *data)
{
    Query *q, *cq, **queries;
    BooleanQuery *bq;
    (void)data;

    queries = ALLOC_N(Query *, 3);
    queries[0] = tq_new("A", "a");
    queries[1] = tq_new("A", "a");
    queries[2] = tq_new("A", "a");

    cq = q_combine(queries, 3);
    Assert(q_eq(cq, queries[1]), "One unique query submitted");
    q_deref(cq);

    Aiequal(1, queries[1]->ref_cnt);
    q_deref(queries[1]);

    q = bq_new(false);
    bq_add_query_nr(q, tq_new("A", "a"), BC_SHOULD);
    bq_add_query_nr(q, tq_new("A", "a"), BC_SHOULD);
    bq_add_query_nr(q, tq_new("A", "a"), BC_SHOULD);

    queries[1] = q;

    cq = q_combine(queries, 3);
    bq = (BooleanQuery *)cq;
    Aiequal(2, bq->clause_cnt);
    Assert(q_eq(bq->clauses[0]->query, queries[0]), "Query should be equal");
    Assert(q_eq(bq->clauses[1]->query, queries[1]), "Query should be equal");
    q_deref(cq);
    q_deref(queries[1]); /* queries[1] */

    q = bq_new(true);
    bq_add_query_nr(q, tq_new("A", "a"), BC_SHOULD);
    bq_add_query_nr(q, tq_new("A", "a"), BC_SHOULD);
    bq_add_query_nr(q, tq_new("A", "a"), BC_SHOULD);

    queries[1] = q;

    cq = q_combine(queries, 3);
    Assert(q_eq(cq, queries[0]), "Again only one unique query submitted");
    q_deref(cq);
    Aiequal(1, queries[0]->ref_cnt);

    bq_add_query_nr(q, tq_new("B", "b"), BC_SHOULD);
    bq_add_query_nr(q, tq_new("C", "c"), BC_SHOULD);

    cq = q_combine(queries, 3);
    Aiequal(BOOLEAN_QUERY, cq->type);

    bq = (BooleanQuery *)cq;
    Aiequal(3, bq->clause_cnt);
    q = tq_new("A", "a");
    Assert(q_eq(bq->clauses[0]->query, q), "Query should be equal");
    q_deref(q);
    q = tq_new("B", "b");
    Assert(q_eq(bq->clauses[1]->query, q), "Query should be equal");
    q_deref(q);
    q = tq_new("C", "c");
    Assert(q_eq(bq->clauses[2]->query, q), "Query should be equal");
    q_deref(q);

    q_deref(cq);
    Aiequal(1, queries[0]->ref_cnt);

    Aiequal(1, queries[2]->ref_cnt);
    q_deref(queries[2]);

    q = bq_new(true);
    bq_add_query_nr(q, tq_new("A", "a"), BC_SHOULD);
    bq_add_query_nr(q, tq_new("B", "b"), BC_SHOULD);
    bq_add_query_nr(q, tq_new("C", "c"), BC_MUST);
    queries[2] = q;

    cq = q_combine(queries, 3);
    Aiequal(BOOLEAN_QUERY, cq->type);

    bq = (BooleanQuery *)cq;
    Aiequal(4, bq->clause_cnt);
    q = tq_new("A", "a");
    Assert(q_eq(bq->clauses[0]->query, q), "Query should be equal");
    q_deref(q);
    q = tq_new("B", "b");
    Assert(q_eq(bq->clauses[1]->query, q), "Query should be equal");
    q_deref(q);
    q = tq_new("C", "c");
    Assert(q_eq(bq->clauses[2]->query, q), "Query should be equal");
    q_deref(q);
    Assert(q_eq(bq->clauses[3]->query, queries[2]), "Query should be equal");

    q_deref(cq);
    Aiequal(1, queries[0]->ref_cnt);

    q_deref(queries[0]);
    q_deref(queries[1]);
    q_deref(queries[2]);
    free(queries);
}

tst_suite *ts_multi_search(tst_suite *suite)
{
    Store *store0 = open_ram_store();
    Store *store1 = open_ram_store();

    IndexReader *ir0, *ir1;
    Searcher **searchers;
    Searcher *searcher;

    suite = tst_add_suite(suite, "test_multi_search");

    prepare_multi_search_index(store0, test_data, 9, 1);
    prepare_multi_search_index(store1, test_data + 9, NELEMS(test_data) - 9, 10);

    ir0 = ir_open(store0);
    ir1 = ir_open(store1);
    searchers = ALLOC_N(Searcher *, 2);
    searchers[0] = isea_new(ir0);
    searchers[1] = isea_new(ir1);
    searcher = msea_new(searchers, 2, true);

    tst_run_test(suite, test_get_doc, (void *)searcher);

    tst_run_test(suite, test_term_query, (void *)searcher);
    tst_run_test(suite, test_boolean_query, (void *)searcher);
    tst_run_test(suite, test_multi_term_query, (void *)searcher);
    tst_run_test(suite, test_phrase_query, (void *)searcher);
    tst_run_test(suite, test_multi_phrase_query, (void *)searcher);
    tst_run_test(suite, test_prefix_query, (void *)searcher);
    tst_run_test(suite, test_range_query, (void *)searcher);
    tst_run_test(suite, test_typed_range_query, (void *)searcher);
    tst_run_test(suite, test_wildcard_query, (void *)searcher);
    tst_run_test(suite, test_search_unscored, (void *)searcher);

    tst_run_test(suite, test_query_combine, NULL);

    store_deref(store0);
    store_deref(store1);
    searcher_close(searcher);
    return suite;
}

