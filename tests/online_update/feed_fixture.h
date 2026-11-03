#ifndef PTC_TEST_UPDATE_FEED_FIXTURE_H
#define PTC_TEST_UPDATE_FEED_FIXTURE_H

#include <stdio.h>

static void test_update_metadata(char *out, size_t size, const char *version, bool pre)
{
    snprintf(out, size,
        "{\"schema_version\":1,\"releases\":[{\"version\":\"%s\",\"tag\":\"v%s\",\"prerelease\":%s,\"notes\":\"notes\","
        "\"package\":{\"name\":\"playwise-%s.zip\",\"size\":3,\"md5\":\"00000000000000000000000000000000\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"gitee_url\":\"https://gitee.com/selfuppen/nx-playwise/releases/download/v%s/playwise-%s.zip\","
        "\"github_url\":\"https://github.com/selfuppen/NX-PlayWise/releases/download/v%s/playwise-%s.zip\"}}]}",
        version, version, pre ? "true" : "false", version, version, version, version, version);
}

#endif
