// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 * Portions Copyright (C) 2013 CohortFS, LLC
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef XIO_MSG_H
#define XIO_MSG_H

#include <boost/intrusive/list.hpp>
#include "msg/SimplePolicyMessenger.h"
extern "C" {
#include "libxio.h"
}
#include "XioConnection.h"
#include "XioSubmit.h"
#include "msg/msg_types.h"
#include "XioPool.h"

namespace bi = boost::intrusive;

class XioMessenger;

class XioMsgCnt
{
public:
  ceph_le32 msg_cnt;
  buffer::list bl;
public:
  explicit XioMsgCnt(buffer::ptr p)
    {
      bl.append(p);
      buffer::list::iterator bl_iter = bl.begin();
      decode(msg_cnt, bl_iter);
    }
};

class XioMsgHdr
{
public:
  char tag;
  ceph_le32 msg_cnt;
  ceph_le32 peer_type;
  entity_addr_t addr; /* XXX hack! */
  ceph_msg_header* hdr;
  ceph_msg_footer* ftr;
  uint64_t features;
  buffer::list bl;
public:
  XioMsgHdr(ceph_msg_header& _hdr, ceph_msg_footer& _ftr, uint64_t _features)
    : tag(CEPH_MSGR_TAG_MSG), msg_cnt(init_le32(0)), hdr(&_hdr), ftr(&_ftr),
      features(_features)
    { }

  XioMsgHdr(ceph_msg_header& _hdr, ceph_msg_footer &_ftr, buffer::ptr p)
    : hdr(&_hdr), ftr(&_ftr)
    {
      bl.append(p);
      buffer::list::iterator bl_iter = bl.begin();
      decode(bl_iter);
    }

  static size_t get_max_encoded_length();

  const buffer::list& get_bl() { encode(bl); return bl; };

  inline void encode_hdr(ceph::buffer::list& bl) const {
    using ceph::encode;
    encode(tag, bl);
    encode(msg_cnt, bl);
    encode(peer_type, bl);
    encode(addr, bl, features);
    encode(hdr->seq, bl);
    encode(hdr->tid, bl);
    encode(hdr->type, bl);
    encode(hdr->priority, bl);
    encode(hdr->version, bl);
    encode(hdr->front_len, bl);
    encode(hdr->middle_len, bl);
    encode(hdr->data_len, bl);
    encode(hdr->data_off, bl);
    encode(hdr->src.type, bl);
    encode(hdr->src.num, bl);
    encode(hdr->compat_version, bl);
    encode(hdr->crc, bl);
  }

  inline void encode_ftr(buffer::list& bl) const {
    using ceph::encode;
    encode(ftr->front_crc, bl);
    encode(ftr->middle_crc, bl);
    encode(ftr->data_crc, bl);
    encode(ftr->sig, bl);
    encode(ftr->flags, bl);
  }

  inline void encode(buffer::list& bl) const {
    encode_hdr(bl);
    encode_ftr(bl);
  }

  inline void decode_hdr(buffer::list::iterator& bl) {
    using ceph::decode;
    decode(tag, bl);
    decode(msg_cnt, bl);
    decode(peer_type, bl);
    decode(addr, bl);
    decode(hdr->seq, bl);
    decode(hdr->tid, bl);
    decode(hdr->type, bl);
    decode(hdr->priority, bl);
    decode(hdr->version, bl);
    decode(hdr->front_len, bl);
    decode(hdr->middle_len, bl);
    decode(hdr->data_len, bl);
    decode(hdr->data_off, bl);
    decode(hdr->src.type, bl);
    decode(hdr->src.num, bl);
    decode(hdr->compat_version, bl);
    decode(hdr->crc, bl);
  }

  inline void decode_ftr(buffer::list::iterator& bl) {
    using ceph::decode;
    decode(ftr->front_crc, bl);
    decode(ftr->middle_crc, bl);
    decode(ftr->data_crc, bl);
    decode(ftr->sig, bl);
    decode(ftr->flags, bl);
  }

  inline void decode(buffer::list::iterator& bl) {
    decode_hdr(bl);
    decode_ftr(bl);
  }

  virtual ~XioMsgHdr()
    {}
};

WRITE_CLASS_ENCODER(XioMsgHdr);

extern struct xio_mempool *xio_msgr_noreg_mpool;

#define XIO_MSGR_IOVLEN 16

struct xio_msg_ex
{
  struct xio_msg msg;
  struct xio_iovec_ex iovs[XIO_MSGR_IOVLEN];

  explicit xio_msg_ex(void* user_context) {
    // go in structure order
    msg.in.header.iov_len = 0;
    msg.in.header.iov_base = NULL;  /* XXX Accelio requires this currently */

    msg.in.sgl_type = XIO_SGL_TYPE_IOV_PTR;
    msg.in.pdata_iov.max_nents = XIO_MSGR_IOVLEN;
    msg.in.pdata_iov.nents = 0;
    msg.in.pdata_iov.sglist = iovs;

    // minimal zero "out" side
    msg.out.header.iov_len = 0;
    msg.out.header.iov_base = NULL;  /* XXX Accelio requires this currently,
				      * against spec */
    // out (some members adjusted later)
    msg.out.sgl_type = XIO_SGL_TYPE_IOV_PTR;
    msg.out.pdata_iov.max_nents = XIO_MSGR_IOVLEN;
    msg.out.pdata_iov.nents = 0;
    msg.out.pdata_iov.sglist = iovs;

    // minimal initialize an "out" msg
    msg.request = NULL;
    msg.type = XIO_MSG_TYPE_ONE_WAY;
    // for now, we DO NEED receipts for every msg
    msg.flags = 0;
    msg.user_context = user_context;
    msg.next = NULL;
    // minimal zero "in" side
  }
};

class XioSend : public XioSubmit
{
public:
  virtual void print_debug(CephContext *cct, const char *tag) const {};
  const struct xio_msg * get_xio_msg() const {return &req_0.msg;}
  struct xio_msg * get_xio_msg() {return &req_0.msg;}
  virtual size_t get_msg_count() const {return 1;}

  XioSend(XioConnection *_xcon, struct xio_reg_mem& _mp, int _ex_cnt=0) :
    XioSubmit(XioSubmit::OUTGOING_MSG, _xcon),
    req_0(this), mp_this(_mp), nrefs(_ex_cnt+1)
  {
    xpool_inc_msgcnt();
    xcon->get();
  }

  XioSend* get() { nrefs++; return this; };

  void put(int n) {
    int refs = nrefs -= n;
    if (refs == 0) {
      struct xio_reg_mem *mp = &this->mp_this;
      this->~XioSend();
      xpool_free(sizeof(XioSend), mp);
    }
  }

  void put() {
    put(1);
  }

  void put_msg_refs() {
    put(get_msg_count());
  }

  virtual ~XioSend() {
    xpool_dec_msgcnt();
    xcon->put();
  }

private:
  xio_msg_ex req_0;
  struct xio_reg_mem mp_this;
  std::atomic<unsigned> nrefs = { 0 };
};

class XioCommand : public XioSend
{
public:
  XioCommand(XioConnection *_xcon, struct xio_reg_mem& _mp):XioSend(_xcon, _mp) {
  }

  buffer::list& get_bl_ref() { return bl; };

private:
  buffer::list bl;
};

struct XioMsg : public XioSend
{
public:
  Message* m;
  XioMsgHdr hdr;
  xio_msg_ex* req_arr;

public:
  XioMsg(Message *_m, XioConnection *_xcon, struct xio_reg_mem& _mp,
	 int _ex_cnt, uint64_t _features) :
    XioSend(_xcon, _mp, _ex_cnt),
    m(_m), hdr(m->get_header(), m->get_footer(), _features),
    req_arr(NULL)
    {
      const entity_inst_t &inst = xcon->get_messenger()->get_myinst();
      hdr.peer_type = inst.name.type();
      hdr.addr = xcon->get_messenger()->get_myaddr_legacy();
      hdr.hdr->src.type = inst.name.type();
      hdr.hdr->src.num = inst.name.num();
      hdr.msg_cnt = _ex_cnt+1;

      if (unlikely(_ex_cnt > 0)) {
	alloc_trailers(_ex_cnt);
      }
    }

  void print_debug(CephContext *cct, const char *tag) const override;
  size_t get_msg_count() const override {
    return hdr.msg_cnt;
  }

  void alloc_trailers(int cnt) {
    req_arr = static_cast<xio_msg_ex*>(malloc(cnt * sizeof(xio_msg_ex)));
    for (int ix = 0; ix < cnt; ++ix) {
      xio_msg_ex* xreq = &(req_arr[ix]);
      new (xreq) xio_msg_ex(this);
    }
  }

  Message *get_message() { return m; }

  ~XioMsg()
    {
      if (unlikely(!!req_arr)) {
	for (unsigned int ix = 0; ix < get_msg_count()-1; ++ix) {
	  xio_msg_ex* xreq = &(req_arr[ix]);
	  xreq->~xio_msg_ex();
	}
	free(req_arr);
      }

      /* testing only! server's ready, resubmit request (not reached on
       * PASSIVE/server side) */
      if (unlikely(m->get_magic() & MSG_MAGIC_REDUPE)) {
	if (likely(xcon->is_connected())) {
	  xcon->send_message(m);
	} else {
	  /* dispose it */
	  m->put();
	}
      } else {
	  /* the normal case: done with message */
	  m->put();
      }
    }
};

class XioDispatchHook : public Message::CompletionHook
{
private:
  XioConnection *xcon;
  XioInSeq msg_seq;
  XioPool rsp_pool;
  std::atomic<unsigned> nrefs { 1 };
  bool cl_flag;
  friend class XioConnection;
  friend class XioMessenger;
public:
  struct xio_reg_mem mp_this;

  XioDispatchHook(XioConnection *_xcon, Message *_m, XioInSeq& _msg_seq,
		    struct xio_reg_mem& _mp) :
    CompletionHook(_m),
    xcon(_xcon->get()),
    msg_seq(_msg_seq),
    rsp_pool(xio_msgr_noreg_mpool),
    cl_flag(false),
    mp_this(_mp)
    {
      ++xcon->n_reqs; // atomicity by portal thread
      xpool_inc_hookcnt();
    }

  virtual void finish(int r) {
    this->put();
  }

  virtual void complete(int r) {
    finish(r);
  }

  int release_msgs();

  XioDispatchHook* get() {
    nrefs++; return this;
  }

  void put(int n = 1) {
    int refs = nrefs -= n;
    if (refs == 0) {
      /* in Marcus' new system, refs reaches 0 twice:  once in
       * Message lifecycle, and again after xio_release_msg.
       */
      if (!cl_flag && release_msgs())
	return;
      struct xio_reg_mem *mp = &this->mp_this;
      this->~XioDispatchHook();
      xpool_free(sizeof(XioDispatchHook), mp);
    }
  }

  XioInSeq& get_seq() { return msg_seq; }

  XioPool& get_pool() { return rsp_pool; }

  void on_err_finalize(XioConnection *xcon) {
    /* can't decode message; even with one-way must free
     * xio_msg structures, and then xiopool
     */
    this->finish(-1);
  }

  ~XioDispatchHook() {
    --xcon->n_reqs; // atomicity by portal thread
    xpool_dec_hookcnt();
    xcon->put();
  }
};

/* A sender-side CompletionHook that relies on the on_msg_delivered
 * to complete a pending mark down. */
class XioMarkDownHook : public Message::CompletionHook
{
private:
  XioConnection* xcon;

public:
  struct xio_reg_mem mp_this;

  XioMarkDownHook(
    XioConnection* _xcon, Message *_m, struct xio_reg_mem& _mp) :
    CompletionHook(_m), xcon(_xcon->get()), mp_this(_mp)
    { }

  virtual void claim(int r) {}

  virtual void finish(int r) {
    xcon->put();
    struct xio_reg_mem *mp = &this->mp_this;
    this->~XioMarkDownHook();
    xio_mempool_free(mp);
  }

  virtual void complete(int r) {
    xcon->_mark_down(XioConnection::CState::OP_FLAG_NONE);
    finish(r);
  }
};

struct XioCompletion : public XioSubmit
{
  XioDispatchHook *xhook;
public:
  XioCompletion(XioConnection *_xcon, XioDispatchHook *_xhook)
    : XioSubmit(XioSubmit::INCOMING_MSG_RELEASE, _xcon /* not xcon! */),
      xhook(_xhook->get()) {
      // submit queue ref
      xcon->get();
    };

  struct xio_msg* dequeue() {
    return xhook->get_seq().dequeue();
  }

  XioDispatchHook* get_xhook() { return xhook; }

  void finalize() {
    xcon->put();
    xhook->put();
  }
};

void print_xio_msg_hdr(CephContext *cct, const char *tag,
		       const XioMsgHdr &hdr, const struct xio_msg *msg);
void print_ceph_msg(CephContext *cct, const char *tag, Message *m);

#endif /* XIO_MSG_H */
