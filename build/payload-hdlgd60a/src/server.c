#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <ps5/kernel.h>
#include "patch.h"
#include "game.h"
#include <sys/select.h>
#include <time.h>
#include "http-cone.h"
#include WEB_HEADER
#include "clock.h"
#include "listener.h"
#include "build-id.h"

static int running=1;
static struct dz_config desired;
static int desired_known,desired_enabled;
static struct { int has; struct dz_config config; const char *error; } saved={.error=""};
static int restore_pid,restore_left;
static uint64_t restore_started;
static int64_t restore_next;
static void load_saved(void) {
    saved.has=!dz_load(DZ_SAVE_PATH,&saved.config);
    saved.error=saved.has || errno==ENOENT?"":"Cannot read saved setting; apply or disable to replace it";
    if (saved.has) { desired=saved.config;desired_enabled=1;desired_known=1; }
}
static void remember_home(const struct dz_state *home) {
    if (!desired_known) { desired=home->config;desired_enabled=home->enabled;desired_known=1; }
}
static void follow_game(void) {
    struct dz_state home;
    if (!dz_read(&home)) {
        remember_home(&home);
        if (home.pid!=restore_pid || home.started!=restore_started) {
            restore_pid=home.pid;restore_started=home.started;
            restore_left=desired_enabled?3:0;restore_next=0;
        }
        if (!desired_enabled || (home.enabled && dz_equal(&home.config,&desired))) restore_left=0;
        int64_t now=dz_milliseconds();
        if (restore_left && now>=0 && now>=restore_next) {
            --restore_left;restore_next=now+5000;
            if (!dz_apply(&desired,1)) { restore_left=0;puts("[deadzone] Saved or requested setting restored"); }
            else printf("[deadzone] Restore failed: %s\n",dz_error());
        }
    }
    if (desired_known) game_tick(&desired,desired_enabled);
    else game_refresh();
}
static int deadline_timeout(int fd, int option, int64_t deadline) {
    int64_t now = dz_milliseconds();
    if (now < 0 || now >= deadline) return -1;
    int64_t remaining = deadline - now;
    struct timeval timeout = {remaining / 1000, (remaining % 1000) * 1000};
    return setsockopt(fd, SOL_SOCKET, option, &timeout, sizeof(timeout));
}
static int same_origin(int fd, const struct request *request) {
    struct sockaddr_in local;
    socklen_t size = sizeof(local);
    char ip[INET_ADDRSTRLEN], host[80], origin[100];
    if (getsockname(fd, (struct sockaddr *)&local, &size) || local.sin_family != AF_INET ||
        !inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip))) return 0;
    snprintf(host, sizeof(host), "%s:%u", ip, (unsigned)ntohs(local.sin_port));
    snprintf(origin, sizeof(origin), "http://%s", host);
    return !strcmp(request->host, host) && (!request->origin[0] || !strcmp(request->origin, origin));
}
static int send_all(int fd, const void *buffer, size_t length) {
    const char *data = buffer;
    int64_t deadline = dz_milliseconds() + 2000;
    while (length) {
        if (deadline_timeout(fd, SO_SNDTIMEO, deadline)) return -1;
        ssize_t sent = send(fd, data, length, 0);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return -1;
        data += sent; length -= (size_t)sent;
    }
    return 0;
}
static void reply(int fd, int status, const char *type, const char *body, size_t length) {
    char header[768];
    int count = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline'; img-src 'self' data:; frame-ancestors 'none'\r\n\r\n",
        status, status == 200 ? "OK" : "Error", type, length);
    if (count > 0 && (size_t)count < sizeof(header) && !send_all(fd, header, (size_t)count))
        send_all(fd, body, length);
}
static void json_error(int fd, int status, const char *message) {
    char json[256];
    int length = snprintf(json, sizeof(json), "{\"error\":\"%s\"}", message);
    if (length > 0 && (size_t)length < sizeof(json)) reply(fd, status, "application/json", json, (size_t)length);
}
static void state_reply(int fd) {
    struct dz_state home={.config=dz_default,.lx=128,.ly=128,.rx=128,.ry=128};
    int home_available=!dz_read(&home);
    const char *home_error=home_available?"":dz_error();
    if (home_available) remember_home(&home);
    struct game_status game;game_read(&game);
    struct dz_state input=game.input.fresh?game.input:home;
    int persisted=home_available && home.enabled==desired_enabled && (!home.enabled || dz_equal(&home.config,&desired)) && !saved.error[0] && desired_known && (desired_enabled?saved.has && dz_equal(&saved.config,&desired):!saved.has);
    const char *source=game.input.fresh?"game":home_available&&home.connected?"home":"none";
    const struct cone_config *hl=&home.config.stick[0],*hr=&home.config.stick[1],*gl=&game.config.stick[0],*gr=&game.config.stick[1];
    uint32_t fw=kernel_get_fw_version();
    char json[1536];
    int length=snprintf(json,sizeof(json),
        "{\"buildId\":\"%s\",\"homeAvailable\":%s,\"homeError\":\"%s\",\"firmware\":\"%x.%02x\",\"pid\":%d,\"enabled\":%s,\"radius\":%d,\"angle\":%d,\"width\":%d,"
        "\"rightRadius\":%d,\"rightAngle\":%d,\"rightWidth\":%d,\"connected\":%s,\"fresh\":%s,\"idle\":%s,"
        "\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"timestamp\":%llu,"
        "\"source\":\"%s\",\"gamePid\":%d,\"gameSupported\":%s,\"gameEnabled\":%s,"
        "\"gameRadius\":%d,\"gameAngle\":%d,\"gameWidth\":%d,"
        "\"gameRightRadius\":%d,\"gameRightAngle\":%d,\"gameRightWidth\":%d,\"gameImports\":%d,\"gameError\":\"%s\",\"saved\":%s,\"saveError\":\"%s\"}",
        DZ_BUILD_ID,home_available?"true":"false",home_error,(unsigned)(fw>>24&0xff),(unsigned)(fw>>16&0xff),home.pid,home.enabled?"true":"false",hl->radius,hl->angle,hl->width,hr->radius,hr->angle,hr->width,input.connected?"true":"false",input.fresh?"true":"false",input.idle?"true":"false",
        input.lx,input.ly,input.rx,input.ry,(unsigned long long)input.timestamp,
        source,game.pid,game.supported?"true":"false",game.enabled?"true":"false",
        gl->radius,gl->angle,gl->width,gr->radius,gr->angle,gr->width,game.imports,game.error,persisted?"true":"false",saved.error);
    if (length>0 && (size_t)length<sizeof(json)) reply(fd,200,"application/json",json,(size_t)length);
}
static int disable(void) {
    desired=dz_default;desired_enabled=0;desired_known=1;restore_left=0;
    int home_result = dz_apply(&dz_default,0);
    if (!dz_forget(DZ_SAVE_PATH)) { saved.has=0;saved.error=""; }
    else saved.error="Cannot clear saved setting; it may return after restart";
    game_sync(&dz_default,0);
    return home_result;
}
static void serve(int fd) {
    char buffer[4096 + 81];
    size_t used = 0;
    int64_t deadline = dz_milliseconds() + 2000;
    char *end = NULL;
    while (used < 4096) {
        if (deadline_timeout(fd, SO_RCVTIMEO, deadline)) return;
        ssize_t got = recv(fd, buffer + used, 4096 - used, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return;
        used += (size_t)got; buffer[used] = 0;
        end = strstr(buffer, "\r\n\r\n");
        if (end) break;
    }
    if (!end) { json_error(fd, 400, "Request too long"); return; }
    size_t header_size = (size_t)(end + 4 - buffer);
    struct request request;
    if (memchr(buffer, 0, header_size) || parse_request(buffer, &request)) {
        json_error(fd, 400, "Invalid request"); return;
    }
    if (!same_origin(fd, &request)) { json_error(fd, 403, "Open the console IP address directly"); return; }
    if (!strcmp(request.method, "GET") && request.length == 0) {
        if (!strcmp(request.path, "/")) reply(fd, 200, "text/html; charset=utf-8", (const char *)web, sizeof(web));
        else if (!strcmp(request.path, "/api/state")) state_reply(fd);
        else json_error(fd, 404, "Not found");
        return;
    }
    if (strcmp(request.method, "POST") || !request.local_header) {
        json_error(fd, 403, "Command not authorized from this page"); return;
    }
    while (used < header_size + request.length) {
        if (deadline_timeout(fd, SO_RCVTIMEO, deadline)) return;
        ssize_t got = recv(fd, buffer + used, header_size + request.length - used, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return;
        used += (size_t)got;
    }
    char *body = buffer + header_size;
    if (memchr(body, 0, request.length)) { json_error(fd, 400, "Invalid value"); return; }
    body[request.length] = 0;
    if (!strcmp(request.path, "/api/apply")) {
        struct dz_config config;
        if (parse_cone(body,&config)) { json_error(fd, 400, "Invalid cone"); return; }
        desired=config;desired_enabled=1;desired_known=1;restore_left=0;
        int home_result=dz_apply(&config,1);
        if (!home_result) {
            if (!dz_save(DZ_SAVE_PATH,&config)) { saved.has=1;saved.config=config;saved.error=""; }
            else saved.error="Applied, but saving for the next boot failed";
        }
        game_sync(&config,1);
        if (home_result) { json_error(fd,409,dz_error());return; }
    } else if (!strcmp(request.path, "/api/disable") && !request.length) {
        if (disable()) { json_error(fd, 409, dz_error()); return; }
    } else if (!strcmp(request.path,"/api/close") && !request.length) {
        /* Always let the server stop: a failed disable() (locate error,
         * partial rollback) must never lock the payload onto its port. */
        int failed=disable();
        running=0;
        if (failed) { json_error(fd,409,dz_error()); return; }
        reply(fd,200,"application/json","{\"closed\":true}",15);return;
    } else { json_error(fd, 404, "Unknown command"); return; }
    state_reply(fd);
}
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int listener=dz_listener(8089);
    if (listener<0) { perror("ps5-dualsense-deadzone bind");return 1; }
    load_saved();
    puts("PS5 DualSense Deadzone ready on port 8089");
    int64_t last_tick=-1000;
    while (running) {
        int64_t now=dz_milliseconds();
        if (now<0) { perror("Monotonic clock unavailable");break; }
        if (now-last_tick>=1000) { follow_game();last_tick=now; }
        fd_set ready;FD_ZERO(&ready);FD_SET(listener,&ready);
        struct timeval wait={1,0};
        int selected=select(listener+1,&ready,NULL,NULL,&wait);
        if (selected<0) { if (errno==EINTR) continue;break; }
        if (!selected) continue;
        int client = accept(listener, NULL, NULL);
        if (client < 0) { if (errno == EINTR) continue; break; }
        serve(client);
        close(client);
    }
    close(listener);
    return running ? 1 : 0;
}
