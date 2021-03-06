/**
   In short, neoagent is distributed under so called "BSD license",

   Copyright (c) 2013 Tatsuhiko Kubo <cubicdaiya@gmail.com>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification,
   are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

   * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

   * Neither the name of the authors nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <time.h>
#include <unistd.h>
#include <limits.h>

#include <json/json.h>

#include "slowlog.h"

void na_slow_query_gettime(na_env_t *env, struct timespec *time)
{
    if ((env->slow_query_sec.tv_sec != 0) ||
        (env->slow_query_sec.tv_nsec != 0))
        clock_gettime(CLOCK_MONOTONIC, time);
}

void na_slow_query_check(na_client_t *client)
{
    na_env_t *env = client->env;
    struct timespec na_to_ts_time, na_from_ts_time, na_to_client_time,
      total_query_time;

    na_difftime(&na_to_ts_time, &client->na_to_ts_time_begin,
                &client->na_to_ts_time_end);
    na_difftime(&na_from_ts_time, &client->na_from_ts_time_begin,
                &client->na_from_ts_time_end);
    na_difftime(&na_to_client_time, &client->na_to_client_time_begin,
                &client->na_to_client_time_end);
    na_addtime(&total_query_time, &na_to_ts_time, &na_from_ts_time);
    na_addtime(&total_query_time, &total_query_time, &na_to_client_time);

    if ((env->slow_query_sec.tv_sec < total_query_time.tv_sec) ||
        ((env->slow_query_sec.tv_sec == total_query_time.tv_sec) &&
         (env->slow_query_sec.tv_nsec < total_query_time.tv_nsec))) {
        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(caddr));
        socklen_t clen = sizeof(caddr);

        if (getpeername(client->cfd, &caddr, &clen) == 0) {
            char host[HOST_NAME_MAX];
            if (gethostname(host, HOST_NAME_MAX) < 0)
                host[HOST_NAME_MAX - 1] = '\0';
            time_t now = time(NULL);
            char *clientaddr = inet_ntoa(caddr.sin_addr);
            uint16_t clientport = ntohs(caddr.sin_port);
            double na_to_ts     = (double)((double)na_to_ts_time.tv_sec     + (double)na_to_ts_time.tv_nsec / 1000000000L),
                   na_from_ts   = (double)((double)na_from_ts_time.tv_sec   + (double)na_from_ts_time.tv_nsec / 1000000000L),
                   na_to_client = (double)((double)na_to_client_time.tv_sec + (double)na_to_client_time.tv_nsec / 1000000000L);

            client->crbuf[client->crbufsize - 2] = '\0'; // don't want newline
            if (env->slow_query_log_format == NA_LOG_FORMAT_JSON) {
                struct json_object *json;
                const size_t bufsz = 128;
                char querybuf[bufsz];

                json = json_object_new_object();
                json_object_object_add(json, "time",         json_object_new_int(now));
                json_object_object_add(json, "type",         json_object_new_string(env->name));
                json_object_object_add(json, "host",         json_object_new_string(host));
                json_object_object_add(json, "clientaddr",   json_object_new_string(clientaddr));
                json_object_object_add(json, "clientport",   json_object_new_int(clientport));
                json_object_object_add(json, "na_to_ts",     json_object_new_double(na_to_ts));
                json_object_object_add(json, "na_from_ts",   json_object_new_double(na_from_ts));
                json_object_object_add(json, "na_to_client", json_object_new_double(na_to_client));
                snprintf(querybuf, bufsz, "%s", client->crbuf);
                json_object_object_add(json, "querytxt",     json_object_new_string(querybuf));

                fprintf(env->slow_query_fp, "%s\n", json_object_to_json_string(json));
                json_object_put(json);
            } else { // plain text format
                fprintf(env->slow_query_fp,
                        "SLOWQUERY: time %lu type %s host %s client %s:%hu na->ts %g na<-ts %g na->c %g querytxt \"%.128s\"\n",
                        now, env->name, host, clientaddr, clientport, na_to_ts, na_from_ts, na_to_client, client->crbuf);
            }
        }
    }

    memset(&client->na_from_ts_time_begin, 0, sizeof(struct timespec));
    memset(&client->na_from_ts_time_end, 0, sizeof(struct timespec));
    memset(&client->na_to_ts_time_begin, 0, sizeof(struct timespec));
    memset(&client->na_to_ts_time_end, 0, sizeof(struct timespec));
    memset(&client->na_to_client_time_begin, 0, sizeof(struct timespec));
    memset(&client->na_to_client_time_end, 0, sizeof(struct timespec));
}

void na_slow_query_open(na_env_t *env)
{
    // close and reopen log file, to permit rotation
    if (env->slow_query_fp)
        fclose(env->slow_query_fp);
    env->slow_query_fp = fopen(env->slow_query_log_path, "a");

    if (!env->slow_query_fp) {
        NA_STDERR_MESSAGE(NA_ERROR_CANT_OPEN_SLOWLOG);
        // disable slow query log, since we couldn't open the file
        memset(&env->slow_query_sec, 0, sizeof(struct timespec));
    }
}
