/* v/http.c
**
*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <uv.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <h2o.h>
#include <h2o/websocket.h>
#include "all.h"
#include "vere/vere.h"

static const c3_i TCP_BACKLOG = 16;

/* _http_vec_to_meth(): convert h2o_iovec_t to meth
*/
static u3_weak
_http_vec_to_meth(h2o_iovec_t vec_u)
{
  return ( 0 == strncmp(vec_u.base, "GET",     vec_u.len) ) ? c3__get  :
         ( 0 == strncmp(vec_u.base, "PUT",     vec_u.len) ) ? c3__put  :
         ( 0 == strncmp(vec_u.base, "POST",    vec_u.len) ) ? c3__post :
         ( 0 == strncmp(vec_u.base, "HEAD",    vec_u.len) ) ? c3__head :
         ( 0 == strncmp(vec_u.base, "CONNECT", vec_u.len) ) ? c3__conn :
         ( 0 == strncmp(vec_u.base, "DELETE",  vec_u.len) ) ? c3__delt :
         ( 0 == strncmp(vec_u.base, "OPTIONS", vec_u.len) ) ? c3__opts :
         ( 0 == strncmp(vec_u.base, "TRACE",   vec_u.len) ) ? c3__trac :
         // TODO ??
         // ( 0 == strncmp(vec_u.base, "PATCH",   vec_u.len) ) ? c3__patc :
         u3_none;
}

/* _http_vec_to_atom(): convert h2o_iovec_t to atom (cord)
*/
static u3_noun
_http_vec_to_atom(h2o_iovec_t vec_u)
{
  return u3i_bytes(vec_u.len, (const c3_y*)vec_u.base);
}

/* _http_vec_to_octs(): convert h2o_iovec_t to (unit octs)
*/
static u3_noun
_http_vec_to_octs(h2o_iovec_t vec_u)
{
  if ( 0 == vec_u.len ) {
    return u3_nul;
  }

  // XX correct size_t -> atom?
  return u3nt(u3_nul, u3i_chubs(1, (const c3_d*)&vec_u.len),
                      _http_vec_to_atom(vec_u));
}

/* _http_vec_from_octs(): convert (unit octs) to h2o_iovec_t
*/
static h2o_iovec_t
_http_vec_from_octs(u3_noun oct)
{
  if ( u3_nul == oct ) {
    return h2o_iovec_init(0, 0);
  }

  //  2GB max
  if ( c3n == u3a_is_cat(u3h(u3t(oct))) ) {
    u3m_bail(c3__fail);
  }

  c3_w len_w  = u3h(u3t(oct));
  c3_y* buf_y = c3_malloc(1 + len_w);
  buf_y[len_w] = 0;

  u3r_bytes(0, len_w, buf_y, u3t(u3t(oct)));

  u3z(oct);
  return h2o_iovec_init(buf_y, len_w);
}

/* _http_heds_to_noun(): convert h2o_header_t to (list (pair @t @t))
*/
static u3_noun
_http_heds_to_noun(h2o_header_t* hed_u, c3_d hed_d)
{
  u3_noun hed = u3_nul;
  c3_d dex_d  = hed_d;

  h2o_header_t deh_u;

  while ( 0 < dex_d ) {
    deh_u = hed_u[--dex_d];
    hed = u3nc(u3nc(_http_vec_to_atom(*deh_u.name),
                    _http_vec_to_atom(deh_u.value)), hed);
  }

  return hed;
}

/* _http_heds_free(): free header linked list
*/
static void
_http_heds_free(u3_hhed* hed_u)
{
  while ( hed_u ) {
    u3_hhed* nex_u = hed_u->nex_u;

    free(hed_u->nam_c);
    free(hed_u->val_c);
    free(hed_u);
    hed_u = nex_u;
  }
}

/* _http_hed_new(): create u3_hhed from nam/val cords
*/
static u3_hhed*
_http_hed_new(u3_atom nam, u3_atom val)
{
  c3_w     nam_w = u3r_met(3, nam);
  c3_w     val_w = u3r_met(3, val);
  u3_hhed* hed_u = c3_malloc(sizeof(*hed_u));

  hed_u->nam_c = c3_malloc(1 + nam_w);
  hed_u->val_c = c3_malloc(1 + val_w);
  hed_u->nam_c[nam_w] = 0;
  hed_u->val_c[val_w] = 0;
  hed_u->nex_u = 0;
  hed_u->nam_w = nam_w;
  hed_u->val_w = val_w;

  u3r_bytes(0, nam_w, (c3_y*)hed_u->nam_c, nam);
  u3r_bytes(0, val_w, (c3_y*)hed_u->val_c, val);

  return hed_u;
}

/* _http_heds_from_noun(): convert (list (pair @t @t)) to u3_hhed
*/
static u3_hhed*
_http_heds_from_noun(u3_noun hed)
{
  u3_noun deh = hed;
  u3_noun i_hed;

  u3_hhed* hed_u = 0;

  while ( u3_nul != hed ) {
    i_hed = u3h(hed);
    u3_hhed* nex_u = _http_hed_new(u3h(i_hed), u3t(i_hed));
    nex_u->nex_u = hed_u;

    hed_u = nex_u;
    hed = u3t(hed);
  }

  u3z(deh);
  return hed_u;
}

/* _http_req_find(): find http request in connection by sequence.
*/
static u3_hreq*
_http_req_find(u3_hcon* hon_u, c3_w seq_l)
{
  u3_hreq* req_u = hon_u->req_u;

  //  XX glories of linear search
  //
  while ( req_u ) {
    if ( seq_l == req_u->seq_l ) {
      return req_u;
    }
    req_u = req_u->nex_u;
  }
  return 0;
}

/* _http_req_link(): link http request to connection
*/
static void
_http_req_link(u3_hcon* hon_u, u3_hreq* req_u)
{
  req_u->hon_u = hon_u;
  req_u->seq_l = hon_u->seq_l++;
  req_u->nex_u = hon_u->req_u;
  hon_u->req_u = req_u;
}

/* _http_req_unlink(): remove http request from connection
*/
static void
_http_req_unlink(u3_hreq* req_u)
{
  u3_hcon* hon_u = req_u->hon_u;

  if ( hon_u->req_u == req_u ) {
    hon_u->req_u = req_u->nex_u;
  }
  else {
    u3_hreq* pre_u = hon_u->req_u;

    //  XX glories of linear search
    //
    while ( pre_u ) {
      if ( pre_u->nex_u == req_u ) {
        pre_u->nex_u = req_u->nex_u;
      }
      else pre_u = pre_u->nex_u;
    }
  }
}

/* _http_req_free(): free http request.
*/
static void
_http_req_free(u3_hreq* req_u)
{
  _http_req_unlink(req_u);
  free(req_u);
}

/* _http_req_new(): receive http request.
*/
static u3_hreq*
_http_req_new(u3_hcon* hon_u, h2o_req_t* rec_u)
{
  u3_hreq* req_u = c3_malloc(sizeof(*req_u));
  req_u->rec_u = rec_u;
  _http_req_link(hon_u, req_u);

  return req_u;
}

/* _http_req_to_duct(): translate srv/con/req to duct
*/
static u3_noun
_http_req_to_duct(u3_hreq* req_u)
{
  return u3nt(u3_blip, c3__http,
              u3nq(u3dc("scot", c3_s2('u','v'), req_u->hon_u->htp_u->sev_l),
                   u3dc("scot", c3_s2('u','d'), req_u->hon_u->coq_l),
                   u3dc("scot", c3_s2('u','d'), req_u->seq_l),
                   u3_nul));
}

/* _http_req_kill(): kill http request in %eyre.
*/
static void
_http_req_kill(u3_hreq* req_u)
{
  u3_noun pox = _http_req_to_duct(req_u);
  u3v_plan(pox, u3nc(c3__thud, u3_nul));
}

/* _http_req_dispatch(): dispatch http request to %eyre
*/
static void
_http_req_dispatch(u3_hreq* req_u, u3_noun req)
{
  u3_noun pox = _http_req_to_duct(req_u);
  u3_noun typ = _(req_u->hon_u->htp_u->lop) ? c3__chis : c3__this;

  u3v_plan(pox, u3nq(typ,
                     req_u->hon_u->htp_u->sec,
                     u3nc(c3y, u3i_words(1, &req_u->hon_u->ipf_w)),
                     req));
}

/* _http_req_respond(): write httr to h2o_req_t->res and send
*/
static void
_http_req_respond(u3_hreq* req_u, u3_noun sas, u3_noun hed, u3_noun bod)
{
  h2o_req_t* rec_u = req_u->rec_u;

  rec_u->res.status = sas;
  rec_u->res.reason = (sas < 200) ? "weird" :
                      (sas < 300) ? "ok" :
                      (sas < 400) ? "moved" :
                      (sas < 500) ? "missing" :
                      "hosed";

  u3_hhed* hed_u = _http_heds_from_noun(u3k(hed));
  u3_hhed* deh_u = hed_u;

  while ( 0 != hed_u ) {
    h2o_add_header_by_str(&rec_u->pool, &rec_u->res.headers,
                          hed_u->nam_c, hed_u->nam_w, 0, 0,
                          hed_u->val_c, hed_u->val_w);
    hed_u = hed_u->nex_u;
  }

  h2o_iovec_t bod_u = _http_vec_from_octs(u3k(bod));
  rec_u->res.content_length = bod_u.len;

  static h2o_generator_t gen_u = {0, 0};
  h2o_start_response(rec_u, &gen_u);

  h2o_send(rec_u, &bod_u, 1, H2O_SEND_STATE_FINAL);

  _http_req_free(req_u);

  // XX allocate on &req_u->rec_u->pool and skip these?
  _http_heds_free(deh_u);
  free(bod_u.base);

  u3z(sas); u3z(hed); u3z(bod);
}

/* _http_rec_to_httq(): convert h2o_req_t to httq
*/
static u3_weak
_http_rec_to_httq(h2o_req_t* rec_u)
{
  u3_noun med = _http_vec_to_meth(rec_u->method);

  if ( u3_none == med ) {
    return u3_none;
  }

  u3_noun url = _http_vec_to_atom(rec_u->path);
  u3_noun hed = _http_heds_to_noun(rec_u->headers.entries,
                                   rec_u->headers.size);

  // restore host header
  hed = u3nc(u3nc(u3i_string("host"),
                  _http_vec_to_atom(rec_u->authority)),
             hed);

  u3_noun bod = _http_vec_to_octs(rec_u->entity);

  return u3nq(med, url, hed, bod);
}

/* _http_rec_fail(): fail on bad h2o_req_t
*/
static void
_http_rec_fail(h2o_req_t* rec_u, c3_i sas_i, c3_c* sas_c)
{
  static h2o_generator_t gen_u = {0, 0};
  rec_u->res.status = sas_i;
  rec_u->res.reason = sas_c;
  h2o_start_response(rec_u, &gen_u);
  h2o_send(rec_u, 0, 0, H2O_SEND_STATE_FINAL);
}

struct h2o_con_wrap {                 //  see private st_h2o_http1_conn_t
  h2o_conn_t         con_u;           //  connection
  struct {                            //  see private st_h2o_uv_socket_t
    h2o_socket_t     sok_u;           //  socket
    uv_stream_t*     han_u;           //  client stream handler (u3_hcon)
  } *suv_u;
};

static void
on_ws_message(h2o_websocket_conn_t *conn,
              const struct wslay_event_on_msg_recv_arg *arg)
{
  if ( 0 == arg ) {
    h2o_websocket_close(conn);
    return;
  }

  if ( !wslay_is_ctrl_frame(arg->opcode) ) {
    struct wslay_event_msg msgarg = {arg->opcode, arg->msg, arg->msg_length};
    wslay_event_queue_msg(conn->ws_ctx, &msgarg);
  }
}

/* _http_rec_accept(); handle incoming http request from h2o.
*/
static c3_i
_http_rec_accept(h2o_handler_t* han_u, h2o_req_t* rec_u)
{
  {
    const c3_c* cik_c;

    if ( (0 == h2o_is_websocket_handshake(rec_u, &cik_c)) && (0 != cik_c) ) {
      // XX use return, pass data, wrap struct, etc.
      h2o_upgrade_to_websocket(rec_u, cik_c, 0, on_ws_message);
      return 0;
    }
  }

  u3_weak req = _http_rec_to_httq(rec_u);

  if ( u3_none == req ) {
    if ( (u3C.wag_w & u3o_verbose) ) {
      uL(fprintf(uH, "strange %.*s request\n", (int)rec_u->method.len,
                                               rec_u->method.base));
    }
    _http_rec_fail(rec_u, 400, "bad request");
  }
  else {
    // XX HTTP2 wat do?
    struct h2o_con_wrap* noc_u = (struct h2o_con_wrap*)rec_u->conn;
    u3_hcon* hon_u = (u3_hcon*)noc_u->suv_u->han_u;

    // sanity check
    c3_assert(hon_u->sok_u == &noc_u->suv_u->sok_u);

    u3_hreq* req_u = _http_req_new(hon_u, rec_u);
    _http_req_dispatch(req_u, req);
  }

  return 0;
}

/* _http_conn_find(): find http connection in server by sequence.
*/
static u3_hcon*
_http_conn_find(u3_http *htp_u, c3_w coq_l)
{
  u3_hcon* hon_u = htp_u->hon_u;

  //  XX glories of linear search
  //
  while ( hon_u ) {
    if ( coq_l == hon_u->coq_l ) {
      return hon_u;
    }
    hon_u = hon_u->nex_u;
  }
  return 0;
}

/* _http_conn_link(): link http request to connection
*/
static void
_http_conn_link(u3_http* htp_u, u3_hcon* hon_u)
{
  hon_u->htp_u = htp_u;
  hon_u->coq_l = htp_u->coq_l++;
  hon_u->nex_u = htp_u->hon_u;
  htp_u->hon_u = hon_u;
}

/* _http_conn_unlink(): remove http request from connection
*/
static void
_http_conn_unlink(u3_hcon* hon_u)
{
  u3_http* htp_u = hon_u->htp_u;

  if ( htp_u->hon_u == hon_u ) {
    htp_u->hon_u = hon_u->nex_u;
  }
  else {
    u3_hcon *pre_u = htp_u->hon_u;

    //  XX glories of linear search
    //
    while ( pre_u ) {
      if ( pre_u->nex_u == hon_u ) {
        pre_u->nex_u = hon_u->nex_u;
      }
      else pre_u = pre_u->nex_u;
    }
  }
}

/* _http_conn_free_early(): free http connection on failure.
*/
static void
_http_conn_free_early(uv_handle_t* han_t)
{
  u3_hcon* hon_u = (u3_hcon*)han_t;
  free(hon_u);
}

/* _http_conn_free(): free http connection on close.
*/
static void
_http_conn_free(uv_handle_t* han_t)
{
  u3_hcon* hon_u = (u3_hcon*)han_t;

  while ( 0 != hon_u->req_u ) {
    u3_hreq* req_u = hon_u->req_u;
    u3_hreq* nex_u = req_u->nex_u;

    _http_req_kill(req_u);
    _http_req_free(req_u);
    hon_u->req_u = nex_u;
  }

  _http_conn_unlink(hon_u);
  free(hon_u);
}

/* _http_conn_new(): create and accept http connection.
*/
static void
_http_conn_new(u3_http* htp_u)
{
  // TODO where?
  // u3_lo_open();

  u3_hcon* hon_u = c3_malloc(sizeof(*hon_u));
  hon_u->seq_l = 1;
  hon_u->req_u = 0;

  uv_tcp_init(u3L, &hon_u->wax_u);

  c3_i sas_i;

  if ( 0 != (sas_i = uv_accept((uv_stream_t*)&htp_u->wax_u,
                               (uv_stream_t*)&hon_u->wax_u)) ) {
    if ( (u3C.wag_w & u3o_verbose) ) {
      uL(fprintf(uH, "http: accept: %s\n", uv_strerror(sas_i)));
    }

    uv_close((uv_handle_t*)&hon_u->wax_u,
             (uv_close_cb)_http_conn_free_early);
    return;
  }

  _http_conn_link(htp_u, hon_u);

  hon_u->sok_u = h2o_uv_socket_create((uv_stream_t*)&hon_u->wax_u,
                                      (uv_close_cb)_http_conn_free);
  h2o_accept(htp_u->cep_u, hon_u->sok_u);

  // capture h2o connection (XX fragile)
  hon_u->con_u = (h2o_conn_t*)hon_u->sok_u->data;

  struct sockaddr_in adr_u;
  h2o_socket_getpeername(hon_u->sok_u, (struct sockaddr*)&adr_u);
  hon_u->ipf_w = ( adr_u.sin_family != AF_INET ) ?
                 0 : ntohl(adr_u.sin_addr.s_addr);

  // TODO where?
  // u3_lo_shut(c3y);
}

/* _http_serv_find(): find http server by sequence.
*/
static u3_http*
_http_serv_find(c3_l sev_l)
{
  u3_http* htp_u = u3_Host.htp_u;

  //  XX glories of linear search
  //
  while ( htp_u ) {
    if ( sev_l == htp_u->sev_l ) {
      return htp_u;
    }
    htp_u = htp_u->nex_u;
  }
  return 0;
}

// XX serv link/unlink/free/new

/* _http_serv_listen_cb(): uv_connection_cb for uv_listen
*/
static void
_http_serv_listen_cb(uv_stream_t* str_u, c3_i sas_i)
{
  u3_http* htp_u = (u3_http*)str_u;

  if ( 0 != sas_i ) {
    uL(fprintf(uH, "http: listen_cb: %s\n", uv_strerror(sas_i)));
  }
  else {
    _http_conn_new(htp_u);
  }
}

/* _http_serv_init_h2o(): initialize h2o ctx and handlers for server.
*/
static void
_http_serv_init_h2o(u3_http* htp_u)
{
  htp_u->fig_u = c3_calloc(sizeof(*htp_u->fig_u));
  h2o_config_init(htp_u->fig_u);
  htp_u->fig_u->server_name = h2o_iovec_init(
                                H2O_STRLIT("urbit/vere-" URBIT_VERSION));

  // XX use u3_Host.ops_u.nam_c? Or ship.urbit.org? Multiple hosts?
  // see https://github.com/urbit/urbit/issues/914
  htp_u->hos_u = h2o_config_register_host(htp_u->fig_u,
                                          h2o_iovec_init(H2O_STRLIT("default")),
                                          htp_u->por_w);

  htp_u->ctx_u = c3_calloc(sizeof(*htp_u->ctx_u));
  htp_u->cep_u = c3_calloc(sizeof(*htp_u->cep_u));
  htp_u->cep_u->ctx = (h2o_context_t*)htp_u->ctx_u;
  htp_u->cep_u->hosts = htp_u->fig_u->hosts;

  if ( c3y == htp_u->sec ) {
    htp_u->cep_u->ssl_ctx = u3_Host.tls_u;
  }

  htp_u->han_u = h2o_create_handler(&htp_u->hos_u->fallback_path,
                                    sizeof(*htp_u->han_u));
  htp_u->han_u->on_req = _http_rec_accept;

  h2o_context_init(htp_u->ctx_u, u3L, htp_u->fig_u);
}

/* _http_serv_start(): start http server.
*/
static void
_http_serv_start(u3_http* htp_u)
{
  struct sockaddr_in adr_u;
  memset(&adr_u, 0, sizeof(adr_u));
  adr_u.sin_family = AF_INET;

  if ( c3y == htp_u->lop ) {
    inet_pton(AF_INET, "127.0.0.1", &adr_u.sin_addr);
  }
  else {
    adr_u.sin_addr.s_addr = INADDR_ANY;
  }

  if ( c3y == htp_u->sec && 0 == u3_Host.tls_u ) {
    uL(fprintf(uH, "http: secure server not started: .urb/tls/ not found\n"));
    htp_u->por_w = 0;
    return;
  }

  uv_tcp_init(u3L, &htp_u->wax_u);

  /*  Try ascending ports.
  */
  while ( 1 ) {
    c3_i sas_i;

    adr_u.sin_port = htons(htp_u->por_w);
    sas_i = uv_tcp_bind(&htp_u->wax_u, (const struct sockaddr*)&adr_u, 0);

    if ( 0 != sas_i ||
         0 != (sas_i = uv_listen((uv_stream_t*)&htp_u->wax_u,
                                 TCP_BACKLOG, _http_serv_listen_cb)) ) {
      if ( UV_EADDRINUSE == sas_i ) {
        htp_u->por_w++;
        continue;
      }

      uL(fprintf(uH, "http: listen: %s\n", uv_strerror(sas_i)));
      htp_u->por_w = 0;
      return;
    }

    _http_serv_init_h2o(htp_u);

    uL(fprintf(uH, "http: live (%s, %s) on %d\n",
                   (c3y == htp_u->sec) ? "secure" : "insecure",
                   (c3y == htp_u->lop) ? "loopback" : "public",
                   htp_u->por_w));
    break;
  }
}

/* _http_init_tls: initialize OpenSSL context
*/
static SSL_CTX*
_http_init_tls()
{
  // XX require 1.1.0 and use TLS_server_method()
  SSL_CTX* tls_u = SSL_CTX_new(SSLv23_server_method());
  // XX use SSL_CTX_set_max_proto_version() and SSL_CTX_set_min_proto_version()
  SSL_CTX_set_options(tls_u, SSL_OP_NO_SSLv2 |
                             SSL_OP_NO_SSLv3 |
                             // SSL_OP_NO_TLSv1 | // XX test
                             SSL_OP_NO_COMPRESSION);

  SSL_CTX_set_default_verify_paths(tls_u);
  SSL_CTX_set_session_cache_mode(tls_u, SSL_SESS_CACHE_OFF);
  SSL_CTX_set_cipher_list(tls_u,
                          "ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:"
                          "ECDH+AES128:DH+AES:ECDH+3DES:DH+3DES:RSA+AESGCM:"
                          "RSA+AES:RSA+3DES:!aNULL:!MD5:!DSS");

  c3_c pub_c[2048];
  c3_c pir_c[2048];
  c3_i ret_i;

  ret_i = snprintf(pub_c, 2048, "%s/.urb/tls/certificate.pem", u3_Host.dir_c);
  c3_assert(ret_i < 2048);
  ret_i = snprintf(pir_c, 2048, "%s/.urb/tls/private.pem", u3_Host.dir_c);
  c3_assert(ret_i < 2048);

  // TODO: SSL_CTX_use_certificate_chain_file ?
  if (SSL_CTX_use_certificate_file(tls_u, pub_c, SSL_FILETYPE_PEM) <= 0) {
    uL(fprintf(uH, "https: failed to load certificate\n"));
    // c3_assert(0);
    return 0;
  }

  if (SSL_CTX_use_PrivateKey_file(tls_u, pir_c, SSL_FILETYPE_PEM) <= 0 ) {
    uL(fprintf(uH, "https: failed to load private key\n"));
    // c3_assert(0);
    return 0;
  }

  return tls_u;
}

/* _http_write_ports_file(): update .http.ports
*/
static void
_http_write_ports_file(c3_c *pax_c)
{
  c3_i    pal_i;
  c3_c    *paf_c;
  c3_i    por_i;
  u3_http *htp_u;

  pal_i = strlen(pax_c) + 13; /* includes NUL */
  paf_c = u3a_malloc(pal_i);
  snprintf(paf_c, pal_i, "%s/%s", pax_c, ".http.ports");

  por_i = open(paf_c, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  u3a_free(paf_c);

  for ( htp_u = u3_Host.htp_u; htp_u; htp_u = htp_u->nex_u ) {
    if ( 0 < htp_u->por_w ) {
      dprintf(por_i, "%u %s %s\n", htp_u->por_w,
                     (c3y == htp_u->sec) ? "secure" : "insecure",
                     (c3y == htp_u->lop) ? "loopback" : "public");
    }
  }

  c3_sync(por_i);
  close(por_i);
}

/* _http_release_ports_file(): remove .http.ports
*/
static void
_http_release_ports_file(c3_c *pax_c)
{
  c3_i pal_i;
  c3_c *paf_c;

  pal_i = strlen(pax_c) + 13; /* includes NUL */
  paf_c = u3a_malloc(pal_i);
  snprintf(paf_c, pal_i, "%s/%s", pax_c, ".http.ports");

  unlink(paf_c);
  u3a_free(paf_c);
}

/* u3_http_ef_bake(): notify %eyre that we're live
*/
void
u3_http_ef_bake(void)
{
  u3_noun pax = u3nq(u3_blip, c3__http, u3k(u3A->sen), u3_nul);

  u3v_plan(pax, u3nc(c3__born, u3_nul));
}

/* u3_http_ef_thou(): send %thou from %eyre as http response.
*/
void
u3_http_ef_thou(c3_l     sev_l,
                c3_l     coq_l,
                c3_l     seq_l,
                u3_noun  rep)
{
  u3_http* htp_u;
  u3_hcon* hon_u;
  u3_hreq* req_u;
  c3_w bug_w = u3C.wag_w & u3o_verbose;

  if ( !(htp_u = _http_serv_find(sev_l)) ) {
    if ( bug_w ) {
      uL(fprintf(uH, "http: server not found: %x\r\n", sev_l));
    }
  }
  else if ( !(hon_u = _http_conn_find(htp_u, coq_l)) ) {
    if ( bug_w ) {
      uL(fprintf(uH, "http: connection not found: %x/%d\r\n", sev_l, coq_l));
    }
  }
  else if ( !(req_u = _http_req_find(hon_u, seq_l)) ) {
    if ( bug_w ) {
      uL(fprintf(uH, "http: request not found: %x/%d/%d\r\n",
                 			sev_l, coq_l, seq_l));
    }
  }
  else {
    u3_noun p_rep, q_rep, r_rep;

    if ( c3n == u3r_trel(rep, &p_rep, &q_rep, &r_rep) ) {
      uL(fprintf(uH, "http: strange response\n"));
    }
    else {
      _http_req_respond(req_u, u3k(p_rep), u3k(q_rep), u3k(r_rep));
    }
  }

  u3z(rep);
}

/* u3_http_io_init(): initialize http I/O.
*/
void
u3_http_io_init()
{
  //  Lens port
  {
    u3_http *htp_u = c3_malloc(sizeof(*htp_u));

    htp_u->sev_l = u3A->sev_l + 2;
    htp_u->coq_l = 1;
    htp_u->por_w = 12321;
    htp_u->sec = c3n;
    htp_u->lop = c3y;

    htp_u->cep_u = 0;
    htp_u->hos_u = 0;
    htp_u->hon_u = 0;
    htp_u->nex_u = 0;

    htp_u->nex_u = u3_Host.htp_u;
    u3_Host.htp_u = htp_u;
  }

  //  Secure port.
  {
    u3_http *htp_u = c3_malloc(sizeof(*htp_u));

    htp_u->sev_l = u3A->sev_l + 1;
    htp_u->coq_l = 1;
    htp_u->por_w = 8443;
    htp_u->sec = c3y;
    htp_u->lop = c3n;

    htp_u->cep_u = 0;
    htp_u->hos_u = 0;
    htp_u->hon_u = 0;
    htp_u->nex_u = 0;

    htp_u->nex_u = u3_Host.htp_u;
    u3_Host.htp_u = htp_u;
  }

   // Insecure port.
  {
    u3_http* htp_u = c3_malloc(sizeof(*htp_u));

    htp_u->sev_l = u3A->sev_l;
    htp_u->coq_l = 1;
    htp_u->por_w = 8080;
    htp_u->sec = c3n;
    htp_u->lop = c3n;

    htp_u->cep_u = 0;
    htp_u->hos_u = 0;
    htp_u->hon_u = 0;
    htp_u->nex_u = 0;

    htp_u->nex_u = u3_Host.htp_u;
    u3_Host.htp_u = htp_u;
  }

  u3_Host.tls_u = _http_init_tls();
}

/* u3_http_io_talk(): start http I/O.
*/
void
u3_http_io_talk()
{
  u3_http* htp_u;

  for ( htp_u = u3_Host.htp_u; htp_u; htp_u = htp_u->nex_u ) {
    _http_serv_start(htp_u);
  }

  _http_write_ports_file(u3_Host.dir_c);
}

/* u3_http_io_poll(): poll kernel for http I/O.
*/
void
u3_http_io_poll(void)
{
}

/* u3_http_io_exit(): shut down http.
*/
void
u3_http_io_exit(void)
{
  // XX shutdown servers cleanly
  _http_release_ports_file(u3_Host.dir_c);
}
