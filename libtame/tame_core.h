
// -*-c++-*-
/* $Id$ */

/*
 *
 * Copyright (C) 2005 Max Krohn (max@okws.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#ifndef _ASYNC_TAME_CORE_H_
#define _ASYNC_TAME_CORE_H_


/*
 * async/tame_core.h
 *
 *   Runtime classes, constants, and MACROs for libasync files that have
 *   been 'tame'd by the tame utility.  That is, all files that have been
 *   translated by tame should #include this file.
 *
 *   Not to be confused with tame/tame.h, which is read only when sfslite
 *   is being compile, and used to make the tame binary.  That tame
 *   (tame/tame.h) is not installed in /usr/local/include..., but this
 *   one is.
 *
 *   The main classes defined are the closure_t baseclass for all
 *   autogenerated closures, and various join_group_t's, used for
 *   NONBLOCK and JOIN constructs.
 *
 */

#include "async.h"
#include "qhash.h"
#include "keyfunc.h"
#include "tame_event.h"
#include "list.h"
#include <pth.h>


/*
 * tame runtime flags
 */
#define   TAME_ERROR_SILENT      (1 << 0)
#define   TAME_ERROR_FATAL       (1 << 1)
#define   TAME_CHECK_LEAKS       (1 << 2)

extern int tame_options;
inline bool tame_check_leaks () { return tame_options & TAME_CHECK_LEAKS ; }

struct nil_t {};

/**
 * not in use, but perhaps useful for "zero"ing out callback::ref's,
 * which might be necessary.
 */
template<class T>
class generic_wrapper_t {
public:
  generic_wrapper_t (T o) : _obj (o) {}
  T obj () const { return _obj; }
private:
  const T _obj;
};


// A set of references
template<class T1=nil_t, class T2=nil_t, class T3=nil_t, class T4=nil_t>
class refset_t {
public:
  refset_t (T1 &r1, T2 &r2, T3 &r3, T4 &r4) 
    :  _r1 (r1), _r2 (r2), _r3 (r3), _r4 (r4) {}
  refset_t (T1 &r1, T2 &r2, T3 &r3)
    : _r1 (r1), _r2 (r2), _r3 (r3), _r4 (_dummy) {}
  refset_t (T1 &r1, T2 &r2)
    : _r1 (r1), _r2 (r2), _r3 (_dummy), _r4 (_dummy) {}
  refset_t (T1 &r1)
    : _r1 (r1), _r2 (_dummy), _r3 (_dummy), _r4 (_dummy) {}
  refset_t ()
    : _r1 (_dummy), _r2 (_dummy), _r3 (_dummy), _r4 (_dummy) {}

  void assign (const T1 &v1, const T2 &v2, const T3 &v3, const T4 &v4) 
  { _r1 = v1; _r2 = v2; _r3 = v3; _r4 = v4; }
  void assign (const T1 &v1, const T2 &v2, const T3 &v3)
  { _r1 = v1; _r2 = v2; _r3 = v3; }
  void assign (const T1 &v1, const T2 &v2) { _r1 = v1; _r2 = v2; }
  void assign (const T1 &v1) { _r1 = v1; }
  void assign () {}

private:
  nil_t _dummy;

  T1 &_r1;
  T2 &_r2;
  T3 &_r3;
  T4 &_r4;
};

/**
 * Weak Reference Counting
 *
 *   Here follows some rudimentary support for 'weak reference counting' 
 *   or WRC for short. The main idea behind WRC is that it is possible
 *   to hold a pointer to an object, and know whether or not it has 
 *   gone out of scope, without keeping it in scope as asycn/refcnt.h
 *   would do.
 *
 *   A class XX that wants to be weak refcounted should inheret from
 *   weak_refcounted_t<XX>.  This will give XX a refcounted bool, which
 *   will be set to true once an object of type XX is destroyed.  References
 *   to that object will hold a reference to this boolean flag, and can
 *   therefore tell when the XX object has gone out of scope.
 *
 *   Given an object xx of class XX that inherets from weak_refcounted_t<XX>, 
 *   get a weak reference to xx by calling weak_ref_t<XX> (&xx).
 *
 *   One possible use for this system is to keep track of what an object's
 *   scope ought to be with a weak_refcounted_t.  Once can incref and
 *   decref such an object as it gains and loses references.  The object
 *   itself is kept in conservatively in scope, though, with true reference 
 *   counting, to avoid memory corruption.  When there is a disagreement
 *   between the weak reference counting and the real reference counting
 *   as to when the object went of out scope, then a warning message
 *   can be generated.
 */

template<class T> class weak_ref_t;

class mortal_t;
class mortal_ref_t {
public:
  mortal_ref_t (mortal_t *m, ptr<ref_flag_t> d1, ptr<ref_flag_t> d2)
    : _mortal (m),
      _destroyed_flag (d1),
      _dead_flag (d2) {}

  void mark_dead ();
private:
  mortal_t *_mortal;
  ptr<ref_flag_t> _destroyed_flag, _dead_flag;
};

/**
 * things can be separately marked dead and destroyed.
 */
class mortal_t {
public:
  mortal_t () : 
    _destroyed_flag (ref_flag_t::alloc (false)),
    _dead_flag (ref_flag_t::alloc (false))
  {}

  virtual ~mortal_t () { _destroyed_flag->set (true); }
  virtual void mark_dead () {}
  ptr<ref_flag_t> destroyed_flag () { return _destroyed_flag; }
  ptr<ref_flag_t> dead_flag () { return _dead_flag; }

  mortal_ref_t make_mortal_ref () 
  { return mortal_ref_t (this, _destroyed_flag, _dead_flag); }

protected:
  ptr<ref_flag_t> _destroyed_flag, _dead_flag;
};


/**
 * A class XX that wants to be weak_refcounted should inherit from
 * weak_refcounted_t<XX>.
 */
template<class T>
class weak_refcounted_t : public mortal_t {
public:
  weak_refcounted_t (T *p) :
    _pointer (p),
    _refcnt (1) {}

  virtual ~weak_refcounted_t () {}

  /**
   * eschew fancy C++ cast operator overloading for ease or readability, etc..
   */
  T * pointer () { return _pointer; }

  /**
   * Make a weak reference to this class, copying over the base pointer,
   * and also the refcounted bool that represents whether or not this
   * class is in scope.
   */
  weak_ref_t<T> make_weak_ref ();
  
  /**
   * One can manually manage the reference count of weak references.
   * When an object weakly leaves scope, it doesn't do anything real
   * (such as deleting an object or setting the _destroyed flag).
   * Rather, it can report an error if a callback for that style of
   * error report was requested.
   */
  void weak_incref () { _refcnt++; }
  void weak_decref () 
  {
    assert ( --_refcnt >= 0);
    if (!_refcnt) {
      cbv::ptr c = _weak_finalize_cb;
      _weak_finalize_cb = NULL;
      if (c) 
	delaycb (0,0,c);
    }
  }
  void set_weak_finalize_cb (cbv::ptr c) { _weak_finalize_cb = c; }
  
private:
  T        *_pointer;
  int _refcnt;
  cbv::ptr _weak_finalize_cb;
};

/**
 * Weak reference: hold a pointer to an object, without increasing its
 * refcount, but still knowing whether or not it's been destroyed.
 * If destroyed, don't access it!
 *
 * @param T a class that implements the method ptr<bool> destroyed_flag()
 */
template<class T>
class weak_ref_t {
public:
  weak_ref_t (weak_refcounted_t<T> *p) : 
    _pointer (p->pointer ()), 
    _destroyed_flag (p->destroyed_flag ()) {}

  /**
   * Access the underlying pointer only after we have ensured that
   * that object it points to is still in scope.
   */
  T * pointer () { return (*_destroyed_flag ? NULL : _pointer); }

  void weak_decref () { if (pointer ()) pointer ()->weak_decref (); }
  void weak_incref () { if (pointer ()) pointer ()->weak_incref (); }

private:
  T                            *_pointer;
  ptr<ref_flag_t>              _destroyed_flag;
};

template<class T> weak_ref_t<T>
weak_refcounted_t<T>::make_weak_ref ()
{
  return weak_ref_t<T> (this);
}


class reenterer_t;
class must_deallocate_t;

// All closures are numbered serially so that our accounting does not
// get confused.
extern u_int64_t closure_serial_number;

class closure_t : public virtual refcount , 
		  public weak_refcounted_t<closure_t>
{
public:
  closure_t (const char *file, const char *fun) ;

  // because it is weak_refcounted, a bool will turn to true once
  // this class is deleted.
  ~closure_t () {}

  virtual bool is_onstack (const void *p) const = 0;

  // manage function reentry
  void set_jumpto (int i) { _jumpto = i; }
  u_int jumpto () const { return _jumpto; }

  u_int64_t id () { return _id; }

  // given a line number of the end of scope, perform sanity
  // checks on scoping, etc.
  void end_of_scope_checks (int line);

  // Associate a join group with this closure
  void associate_join_group (mortal_ref_t mref, const void *jgwp);

  // Account for join groups that **should** be going out of
  // scope as we are weakly going out of scope
  void semi_deallocate_coordgroups ();

  // Once we're done allocating, we then associate all join groups
  // with this closure, based on the is_onstack test.
  void collect_join_groups ();

  // Initialize a block environment with the ID of this block
  // within the given function.  Also reset any internal counters.
  void init_block (int blockid, int lineno);

  // Make a new closure wrapper. A closure wrapper is something that 
  // hold onto the closure, but also registers itself with the closure
  // internally, so that per-callback accounting can be performed.
  ptr<reenterer_t> make_reenter (const char *loc);

  u_int _jumpto;

  // Display full location with filename, line number and also
  // function name.
  str loc (int lineno) const;
  void error (int lineno, const char *msg);

  ptr<must_deallocate_t> must_deallocate () { return _must_deallocate; }

  // Reenter the function with the appropriate args.
  virtual void reenter () = 0;

  // If the block count is appropriate, then reenter the function.
  void maybe_reenter (const char *loc);

  // Decremenet the block count; return TRUE if it goes down to 0, signifying
  // contuination inside the function.
  bool block_dec_count (const char *loc);

protected:

  u_int64_t _id;

  // All of the join groups that are within our bounds
  vec<mortal_ref_t> _join_groups;
  
  const char *_filename;              // filename for the function
  const char *_funcname;            

  ptr<must_deallocate_t> _must_deallocate; // must dealloc as control leaves

  // Variables involved with managing BLOCK blocks. Note that only one
  // can be active at any given time.
public:
  struct block_t { 
    block_t () : _id (0), _count (0), _lineno (0) {}
    int _id, _count, _lineno;
  };
  block_t _block;

};

struct must_deallocate_obj_t {
  virtual ~must_deallocate_obj_t () {}
  virtual const char *loc () const = 0;
  virtual const char *must_dealloc_typ () const = 0;
  list_entry<must_deallocate_obj_t> _lnk;
};

class must_deallocate_t {
public:
  must_deallocate_t () {}
  void check ();
  void add (must_deallocate_obj_t *o) { _objs.insert_head (o); }
  void rem (must_deallocate_obj_t *o) { _objs.remove (o); }
private:
  list<must_deallocate_obj_t, &must_deallocate_obj_t::_lnk> _objs;
};


//
// An interface used for reentering a function from within a 
// cwait {} block with an implicit rendezvous.
//
class reenterer_t : public virtual refcount, public must_deallocate_obj_t {
public:
  reenterer_t (ptr<must_deallocate_t> md, const char *loc);
  virtual ~reenterer_t ();
  virtual void maybe_reenter () = 0;
  const char *loc () const { return _loc; }
public:
  list_entry<must_deallocate_obj_t> _lnk;
protected:
  ptr<must_deallocate_t> _md;
  const char *_loc;
};

//
// A wrapper around a closure pointer, in turn wrapped into a C.V.
// If a C.V. never goes out of scope, the wrapper won't go out of
// scope.  Wrappers left in scope after closure semi-deallocation
// trigger warnings.
//
class closure_reenter_t : public reenterer_t {
public:
  closure_reenter_t (ptr<closure_t> c, const char *loc);
  ~closure_reenter_t ();

  const char *must_dealloc_typ () const { return "event"; }
  void maybe_reenter () { _cls->maybe_reenter (_loc); }

private:
  ptr<closure_t> _cls;
};

template<class T1 = nil_t, class T2 = nil_t, class T3 = nil_t, class T4 = nil_t>
struct value_set_t {
  value_set_t () {}
  value_set_t (T1 v1) : v1 (v1) {}
  value_set_t (T1 v1, T2 v2) : v1 (v1), v2 (v2) {}
  value_set_t (T1 v1, T2 v2, T3 v3) : v1 (v1), v2 (v2), v3 (v3) {}
  value_set_t (T1 v1, T2 v2, T3 v3, T4 v4) 
    : v1 (v1), v2 (v2), v3 (v3), v4 (v4) {}

  T1 v1;
  T2 v2;
  T3 v3;
  T4 v4;
};

/**
 * functions defined in tame.C, mainly for reporting errors, and
 * determinig what will happen when an error occurs. Change the
 * runtime behavior of what happens in an error via TAME_OPTIONS
 */
void tame_error (const char *loc, const char *msg);
INIT(tame_init);

void collect_join_group (mortal_ref_t r, void *p);

typedef enum { JOIN_NONE = 0,
	       JOIN_EVENTS = 1,
	       JOIN_THREADS = 2 } join_method_t;


/**
 * Holds the important information associated with a join group,
 * such as how many calls are oustanding, and who is waiting to join.
 * Therefore has the callbacks to match joiners up with those waiting
 * to join.
 */
template<class T1 = nil_t, class T2 = nil_t, class T3 = nil_t, class T4 = nil_t>
class join_group_pointer_t 
  : public virtual refcount ,
    public weak_refcounted_t<join_group_pointer_t<T1,T2,T3,T4> >
{
public:
  join_group_pointer_t (const char *f, int l) : 
    weak_refcounted_t<join_group_pointer_t<T1,T2,T3,T4> > (this),
    _n_out (0), _file (f), _lineno (l),
    _must_deallocate (New refcounted<must_deallocate_t> ()),
    _join_method (JOIN_NONE) 
  {
    pth_mutex_init (&_mutex);
    pth_cond_init (&_cond);
  }


  virtual ~join_group_pointer_t () { mark_dead (); }

  void mark_dead ()
  { 
    if (*mortal_t::dead_flag ())
      return;
    mortal_t::dead_flag ()->set (true);

    // XXX - find some way to identify this join, either by filename
    // and line number, or other ways.
    if (need_join ()) {
      str s1 = (_file && _lineno) ?
	str (strbuf ("%s:%d", _file, _lineno)) :
	str ("(unknown)");
      strbuf b;
      b.fmt ("coordgroup went out of scope when expecting %d signal(s)",
	     n_joins_left ());
      str s2 = b;
      tame_error (s1.cstr (), s2.cstr ());
    }

    if (tame_check_leaks ())
      // Check for any leaked coordination variables
      delaycb (0, 0, wrap (_must_deallocate, &must_deallocate_t::check));
  }

  void set_join_cb (cbv::ptr c) 
  { set_join_method (JOIN_EVENTS); _join_cb = c; }

  void collect_myself (void *jgwp) 
  { collect_join_group (mortal_t::make_mortal_ref (), jgwp); }

  void launch_one (ptr<closure_t> c = NULL)
  { 
    add_join ();
  }

  inline void add_join () { _n_out ++; }
  void remove_join () { assert (_n_out-- > 0);}

  void join (value_set_t<T1, T2, T3, T4> v) 
  {
    _n_out --;
    _pending.push_back (v);
    if (_join_method == JOIN_EVENTS) {
      assert (_join_cb);
      cbv cb = _join_cb;
      _join_cb = NULL;
      (*cb) ();
    } else if (_join_method == JOIN_THREADS) {
      pth_cond_notify (&_cond, 0);
    }
  }


  void threadjoin ()
  {
    set_join_method (JOIN_THREADS);
    await ();
    clear_join_method ();
  }

  void set_join_method (join_method_t jm)
  {
    assert (_join_method == JOIN_NONE);
    _join_method = jm;
  }

  void clear_join_method () 
  { 
    if (_join_method == JOIN_EVENTS) {
      _join_cb = NULL;
    }
    _join_method = JOIN_NONE; 
  }

  ptr<must_deallocate_t> must_deallocate () { return _must_deallocate; }

  u_int n_pending () const { return _pending.size (); }
  u_int n_out () const { return _n_out; }
  u_int n_joins_left () const { return _n_out + _pending.size (); }
  bool need_join () const { return n_joins_left () > 0; }

  static value_set_t<T1,T2,T3,T4> to_vs ()
  { return value_set_t<T1,T2,T3,T4> (); }

  bool pending (value_set_t<T1, T2, T3, T4> *p = NULL)
  {
    bool ret = false;
    if (_pending.size ()) {
      if (p) *p = _pending.pop_front ();
      else _pending.pop_front ();
      ret = true;
    }
    return ret;
  }

  bool pending ()
  {
    bool ret = false;
    if (_pending.size ()) {
      _pending.pop_front ();
      ret = true;
    }
    return ret;
  }

private:

  void await ()
  {
    pth_mutex_acquire (&_mutex, 0, NULL);
    pth_cond_await (&_cond, &_mutex, NULL);
  }

  // number of calls out that haven't yet completed
  u_int _n_out; 

  // number of completing calls that are waiting for a join call
  vec<value_set_t<T1, T2, T3, T4> > _pending;

  // callback to call once
  cbv::ptr _join_cb;

  // try to debug leaked joiners
  const char *_file;
  int _lineno;

  ptr<must_deallocate_t> _must_deallocate;

  // for threads
  join_method_t _join_method;
  pth_cond_t _cond;
  pth_mutex_t _mutex;
};

/**
 * Joiners can theoretically persist past when the join group went
 * out of scope; they shouldn't keep join groups in scope just because
 * they haven't fired yet -- this would lead to memory leaks.  Therefore,
 * a joiner holds a *weak reference* to a join group.  If the join group
 * went out of strong scope before we had a choice to join, there was
 * definitely a problem, and we report it.
 */
template<class T1 = nil_t, class T2 = nil_t, class T3 = nil_t, class T4 = nil_t>
class joiner_t : public virtual refcount,
		 public must_deallocate_obj_t {
public:
  joiner_t (ptr<join_group_pointer_t<T1,T2,T3,T4> > p, const char *l,
	    ptr<must_deallocate_t> md) 
    : _weak_ref (p->make_weak_ref ()), _loc (l), _must_deallocate (md),
      _joined (false)
  {
    _must_deallocate->add (this);
  }

  ~joiner_t () 
  { 
    if (!_joined && _weak_ref.pointer ()) {
      _weak_ref.pointer ()->remove_join ();
    }
    _must_deallocate->rem (this); 
  }

  void join (value_set_t<T1,T2,T3,T4> w)
  {
    _joined = true;

    // Need at least one return to the bottom event loop between
    // a call into the callback and a join.
    delaycb (0, 0, wrap (mkref (this), &joiner_t<T1,T2,T3,T4>::join_cb, w));
  }

  const char *loc () const { return _loc; }
  const char *must_dealloc_typ () const 
  { return "non-block event"; }
  
private:
  void error ()
  {
    tame_error (_loc, "coordgroup_t went out of scope before signal");
  }

  // cannot join if the underlying join group went out of scope.
  void join_cb (value_set_t<T1,T2,T3,T4> w)
  {
    if (!_weak_ref.pointer ()) {
      error ();
    } else {
      _weak_ref.pointer ()->join (w);
    }
  }

  weak_ref_t<join_group_pointer_t<T1,T2,T3,T4> > _weak_ref;
  const char *_loc;
  ptr<must_deallocate_t> _must_deallocate;
  bool _joined;
};


/**
 * @brief A wrapper class around a join group pointer for tighter code
 */
template<class T1 = nil_t, class T2 = nil_t, class T3 = nil_t, class T4 = nil_t>
class join_group_t {
public:
  join_group_t (const char *f = NULL, int l = 0) 
    : _pointer (New refcounted<join_group_pointer_t<T1,T2,T3,T4> > (f, l)) 
  { _pointer->collect_myself (static_cast<void *> (this)); }

  join_group_t (ptr<join_group_pointer_t<T1,T2,T3,T4> > p) : _pointer (p) {}

  //-----------------------------------------------------------------------
  //
  // The following functions are used by the rewriter, but can also
  // be accessed by the programmer when manipulating join groups
  //

  /**
   * Register another outstanding callback to join; useful in the case
   * of "sticky" callbacks the fire more than once.  If you expect a 
   * callback generated with '@' to call more than once, call 'add_join'
   * for all but the first callback fire.
   */
  void add_join () { _pointer->add_join (); }

  /**
   * Unregister a callback, in case a callback was canceled before it
   * fired.
   */
  void remove_join () { _pointer->remove_join (); }

  /**
   * Determine the number of callbacks that have fired, but are waiting
   * for a join.
   */
  u_int n_pending () const { return _pointer->n_pending (); }

  /**
   * Determine the number of callbacks still out, that have yet to fire.
   */
  u_int n_out () const { return _pointer->n_out (); }

  /**
   * Determine the total number of joins left to do before the join group
   * has been drained.  Number of joins left to do is determined by adding
   * the number of callbacks left to fire to the number of callbacks 
   * already fired, and waiting for a join.
   */
  u_int n_joins_left () const { return _pointer->n_joins_left (); }

  /**
   * On if an additional join is needed.
   */
  bool need_join () const { return _pointer->need_join (); }

  /**
   * Get the next pending event; returns true and gives the values of that
   * event if there is, in fact, a callback that has fired, but hasn't
   * been joined on yet.  Returns false if there are no pending events
   * to join on.
   */
  bool pending (value_set_t<T1, T2, T3, T4> *p = NULL)
  { return _pointer->pending (p); }

  //
  // end of public functions that the programmer should call
  //-----------------------------------------------------------------------

  //-----------------------------------------------------------------------
  //  
  // The following functions are public because the need to be accessed
  // by the tame rewriter; they should not be called directly by 
  // programmers.
  void set_join_cb (cbv::ptr c) { _pointer->set_join_cb (c); }
  void launch_one (ptr<closure_t> c = NULL) 
  { _pointer->launch_one (c); }

  ptr<join_group_pointer_t<T1,T2,T3,T4> > pointer () { return _pointer; }
  static value_set_t<T1,T2,T3,T4> to_vs () 
  { return value_set_t<T1,T2,T3,T4> (); }

  ptr<joiner_t<T1,T2,T3,T4> > make_joiner (const char *loc)
  { 
    return New refcounted<joiner_t<T1,T2,T3,T4> > 
      (_pointer, loc, _pointer->must_deallocate ()); 
  }

  void clear_join_method () { _pointer->clear_join_method (); }

  //
  //-----------------------------------------------------------------------

private:
  ptr<join_group_pointer_t<T1,T2,T3,T4> > _pointer;
};

/**
 * A coordgroup_t (short for coordination group) is a synonym for 
 * join_group_t, for use with WAIT as opposed to join. Coordination variables
 * join a particular coordination group when they are dispatched.  When
 * a signal is made on a coordination variable, the coordination groups
 * gets the message, and processes it accordingly. 
 * 
 */
template<class T1 = nil_t, class T2 = nil_t, class T3 = nil_t, class T4 = nil_t>
class coordgroup_t : public join_group_t<T1,T2,T3,T4>
{
public:
  coordgroup_t (const char *f = NULL, int l = 0) : 
    join_group_t<T1,T2,T3,T4> (f, l) {}
  coordgroup_t (ptr<join_group_pointer_t<T1,T2,T3,T4> > p) : 
    join_group_t<T1,T2,T3,T4> (p) {}

  void remove_var () { join_group_t<T1,T2,T3,T4>::remove_join (); }
  void add_var () { join_group_t<T1,T2,T3,T4>::add_join (); }
  
  /**
   * The number of signals left to happen; a sum of those
   * pending and those that have yet to fire.
   */
  u_int n_vars_left () const 
  { return join_group_t<T1,T2,T3,T4>::n_joins_left (); }

  /**
   * Synonym for the above, since n_signals_left was in the
   * paper.
   */
  u_int n_signals_left () const { return n_vars_left (); }

  /**
   * Whether we still should be waiting on this coordgroup or not
   */
  bool need_wait () const { return n_vars_left () > 0; }
  
  /**
   * Get the next signal, and put the results into the given
   * slots. Return true if there is an signal pending, and false
   * otherwise.
   */
  bool next_var (T1 &r1, T2 &r2, T3 &r3, T4 &r4)
  {
    bool ret = true;
    value_set_t<T1,T2,T3,T4> v;
    if (pending (&v)) {
      r1 = v.v1;
      r2 = v.v2;
      r3 = v.v3;
      r4 = v.v4;
    } else
      ret = false;
    return ret;
  }
  bool next_signal (T1 &r1, T2 &r2, T3 &r3, T4 &r4) 
  { return next_var (r1, r2, r3, r4); }

  bool next_var (T1 &r1, T2 &r2, T3 &r3)
  {
    bool ret = true;
    value_set_t<T1,T2,T3> v;
    if (pending (&v)) {
      r1 = v.v1;
      r2 = v.v2;
      r3 = v.v3;
    } else
      ret = false;
    return ret;
  }
  bool next_signal (T1 &r1, T2 &r2, T3 &r3) { return next_var (r1, r2, r3); }

  bool next_var (T1 &r1, T2 &r2)
  {
    bool ret = true;
    value_set_t<T1,T2> v;
    if (pending (&v)) {
      r1 = v.v1;
      r2 = v.v2;
    } else
      ret = false;
    return ret;
  }
  bool next_signal (T1 &r1, T2 &r2) { return next_var (r1); }

  bool next_var (T1 &r1)
  {
    bool ret = true;
    value_set_t<T1> v;
    if (pending (&v)) {
      r1 = v.v1;
    } else
      ret = false;
    return ret;
  }
  bool next_signal (T1 &r1) { return next_var (r1); }

  bool next_var () { return join_group_t<T1,T2,T3,T4>::pending (); }
  bool next_signal () { return next_var (); }


  void wait (T1 &r1, T2 &r2, T3 &r3, T4 &r4)
  {
    this->pointer ()->threadjoin ();
    assert (next_var (r1, r2, r3, r4));
  }

  void wait (T1 &r1, T2 &r2, T3 &r3)
  {
    this->pointer ()->threadjoin ();
    assert (next_var (r1, r2, r3));
  }

  void wait (T1 &r1, T2 &r2)
  {
    this->pointer ()->threadjoin ();
    assert (next_var (r1, r2));
  }

  void wait (T1 &r1)
  {
    this->pointer ()->threadjoin ();
    assert (next_var (r1));
  }

  void wait ()
  {
    this->pointer ()->threadjoin ();
    assert (next_var ());
  }

  void waitall () { while (need_wait ()) wait (); }
};

class stack_reenter_t : public reenterer_t {
public:
  stack_reenter_t (ptr<must_deallocate_t> sentinel, 
		   const char *l, coordgroup_t<> jg);
  
  ~stack_reenter_t ();
  const char *must_dealloc_typ () const { return "thread-based event"; }
  void maybe_reenter () { _joiner->join (value_set_t<> ()); }
private:
  ptr<joiner_t<> > _joiner;
};

class implicit_rendezvous_t {
public:
  virtual ~implicit_rendezvous_t () {}
  virtual ptr<reenterer_t> make_reenter (const char *loc) = 0;
};

class closure_implicit_rendezvous_t : public implicit_rendezvous_t {
public:
  closure_implicit_rendezvous_t (ptr<closure_t> c) : _cls (c) {}
  ptr<reenterer_t> make_reenter (const char *loc)
  { return _cls->make_reenter (loc); }
private:
  ptr<closure_t> _cls;
};

class threaded_implicit_rendezvous_t : public implicit_rendezvous_t {
public:
  threaded_implicit_rendezvous_t (const char *f, int l) 
    : _md (New refcounted<must_deallocate_t> ()),
      _jg (f, l) {}
  ~threaded_implicit_rendezvous_t () { _jg.waitall (); }

  ptr<reenterer_t> make_reenterer (const char *loc)
  { return New refcounted<stack_reenter_t> (_md, loc, _jg); }
private:
  ptr<must_deallocate_t> _md;
  coordgroup_t<> _jg;
};

template<class T> void use_reference (T &i) {}

// make shortcuts to the most common callbacks, but while using
// ptr's, and not ref's.

typedef ref<callback<void, bool> > event_bool_t;
typedef ref<callback<void, int> > event_int_t;
typedef ref<callback<void, void> > event_void_t;
typedef ref<callback<void, str> > event_str_t;

template<class T1 = void, class T2 = void, class T3 = void>
struct event {
  typedef ref<callback<void, T1, T2, T3> > t;
};

#define TAME_GLOBAL_INT      tame_global_int
#define CLOSURE              ptr<closure_t> __frame = NULL
#define TAME_OPTIONS         "TAME_OPTIONS"
#define __CLS                 __cls     

extern int TAME_GLOBAL_INT;

void start_join_group_collection ();

#define LOC(f,l) f ":" #l
#define HERE LOC(__FILE__, __LINE__)
#define mkevent(...) _mkevent (__cls_g, HERE, ## __VA_ARGS__)

#define rendezvous_t coordgroup_t

#endif /* _ASYNC_TAME_CORE_H_ */
