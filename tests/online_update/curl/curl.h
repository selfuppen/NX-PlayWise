#ifndef PTC_TEST_UPDATE_CURL_H
#define PTC_TEST_UPDATE_CURL_H
/* Deterministic transport boundary; no host network calls or TLS claims. */
typedef struct TestCurl CURL;
typedef int CURLcode;
typedef long long curl_off_t;
#define CURLE_OK 0
#define CURLE_SSL_CACERT_BADFILE 77
#define CURL_GLOBAL_DEFAULT 0
#define CURLPROTO_HTTPS 2
#define CURLVERSION_NOW 0
typedef struct { const char *version, *ssl_version; } curl_version_info_data;
curl_version_info_data *curl_version_info(int age);
enum {
    CURLOPT_URL, CURLOPT_PROTOCOLS, CURLOPT_REDIR_PROTOCOLS, CURLOPT_FOLLOWLOCATION,
    CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYHOST, CURLOPT_CONNECTTIMEOUT, CURLOPT_TIMEOUT,
    CURLOPT_LOW_SPEED_LIMIT, CURLOPT_LOW_SPEED_TIME, CURLOPT_NOSIGNAL, CURLOPT_USERAGENT,
    CURLOPT_WRITEFUNCTION, CURLOPT_WRITEDATA, CURLOPT_HEADERFUNCTION, CURLOPT_HEADERDATA,
    CURLOPT_XFERINFOFUNCTION, CURLOPT_XFERINFODATA, CURLOPT_NOPROGRESS,
    CURLOPT_SSL_CTX_FUNCTION, CURLOPT_SSL_CTX_DATA,
    CURLINFO_RESPONSE_CODE, CURLINFO_REDIRECT_URL, CURLINFO_OS_ERRNO, CURLINFO_SSL_VERIFYRESULT
};
int curl_global_init(long flags);
void curl_global_cleanup(void);
CURL *curl_easy_init(void);
void curl_easy_cleanup(CURL *curl);
int curl_easy_setopt(CURL *curl, int option, ...);
int curl_easy_perform(CURL *curl);
int curl_easy_getinfo(CURL *curl, int info, ...);
#endif
