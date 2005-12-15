
#include "tame.h"
#include "rxx.h"

//-----------------------------------------------------------------------
// output only the generic callbacks that we need, to speed up compile
// times.
//
bhash<u_int> _generic_cb_tab;

static u_int cross (u_int a, u_int b) 
{
  assert (a <= 0xff && b <= 0xff);
  return ((a << 8) | b);
}

bool generic_cb_exists (u_int a, u_int b) 
{ return _generic_cb_tab[cross (a,b)]; }

void generic_cb_declare (u_int a, u_int b)
{ _generic_cb_tab.insert (cross (a,b)); }

//
//-----------------------------------------------------------------------


var_t::var_t (const str &t, ptr<declarator_t> d, vartyp_t a)
  : _type (t, d->pointer ()), _name (d->name ()), _asc (a), 
    _initializer (d->initializer ()) {}

//
// note that a callback ID is only necessary in the (optimized case)
// of adding a callback to a BLOCK {...} block within a function
//
tame_block_callback_t::tame_block_callback_t (tame_fn_t *f, 
						  tame_block_t *g, 
						  ptr<expr_list_t> l) 
  : tame_callback_t (l), _parent_fn (f), _block (g),
    _cb_ind (_parent_fn->add_callback (this))
{}

static void output_el (int fd, tame_el_t *e) { e->output (fd); }
void element_list_t::output (int fd) { _lst.traverse (wrap (output_el, fd)); }

#define CLOSURE               "__cls"
#define CLOSURE_RFCNT         "__cls_r"
#define CLOSURE_GENERIC       "__cls_g"

str
type_t::mk_ptr () const
{
  strbuf b;
  b << "ptr<" << _base_type << " >";
  return b;
}

str
type_t::alloc_ptr (const str &n, const str &args) const
{
  my_strbuf_t b;
  b.mycat (mk_ptr ()) << " " << n << " = New refcounted<"
		      << _base_type << " > (" << args << ")";
  return b;
}

const var_t *
vartab_t::lookup (const str &n) const
{
  const u_int *ind = _tab[n];
  if (!ind) return NULL;
  return &_vars[*ind];
}

str
type_t::to_str () const
{
  strbuf b;
  b << _base_type << " ";
  if (_pointer)
    b << _pointer;
  return b;
}

str
var_t::decl () const
{
  strbuf b;
  b << _type.to_str () << _name;
  return b;
}

str
var_t::decl (const str &p, int n) const
{
  strbuf b;
  b << _type.to_str () << p << n;
  return b;
}

str
var_t::ref_decl () const
{
  strbuf b;
  b << _type.to_str () << "&" << _name;
  return b;
}

str
mangle (const str &in)
{
  const char *i;
  char *o;
  mstr m (in.len ());
  for (i = in.cstr (), o = m.cstr (); *i; i++, o++) {
    *o = (*i == ':' || *i == '<' || *i == '>' || *i == ',') ? '_' : *i;
  }
  return m;
}

str 
strip_to_method (const str &in)
{
  static rxx mthd_rxx ("([^:]+::)+");

  if (mthd_rxx.search (in)) {
    return str (in.cstr () + mthd_rxx[1].len ());
  }
  return in;
}

str 
strip_off_method (const str &in)
{
  static rxx mthd_rxx ("([^:]+::)+");

  if (mthd_rxx.search (in)) 
    return str (in.cstr (), mthd_rxx[1].len () - 2);

  return NULL;
}


bool
vartab_t::add (var_t v)
{
  if (_tab[v.name ()]) {
    return false;
  }

  _vars.push_back (v);
  _tab.insert (v.name (), _vars.size () - 1);

  return true;
}

void
declarator_t::dump () const
{
  warn << "declarator dump:\n";
  if (_name)
    warn << "  name: " << _name << "\n";
  if (_pointer)
    warn << "  pntr: " << _pointer << "\n";
  if (_params)
    warn << "  param list size: " << _params->size () << "\n";
}

void
element_list_t::passthrough (const lstr &s)
{
  if (!*_lst.plast || !(*_lst.plast)->append (s)) 
    _lst.insert_tail (New tame_passthrough_t (s));

}

void
parse_state_t::new_block (tame_block_t *g)
{
  _block = g;
  push (g);
}

void
parse_state_t::new_join (tame_join_t *j)
{
  _join = j;
  push (j);
}

void
parse_state_t::new_nonblock (tame_nonblock_t *b)
{
  _nonblock = b;
  push (b);
}

void
tame_fn_t::add_env (tame_env_t *e)
{
  _envs.push_back (e); 
  if (e->is_jumpto ()) 
    e->set_id (++_n_labels);
}

//-----------------------------------------------------------------------
// Output utility routines
//

var_t
tame_fn_t::closure_generic ()
{
  return var_t ("ptr<closure_t>", NULL, CLOSURE_GENERIC);
}

var_t
tame_fn_t::trig ()
{
  return var_t ("ptr<trig_t>", NULL, "trig");
}

var_t
tame_fn_t::mk_closure () const
{
  strbuf b;
  b << _name_mangled << "__closure_t";

  return var_t (b, "*", CLOSURE);
}

str
tame_fn_t::decl_casted_closure (bool do_lhs) const
{
  strbuf b;
  if (do_lhs) {
    b << "  " << _closure.decl ()  << " =\n";
  }
  b << "    reinterpret_cast<" << _closure.type ().to_str () 
    << "> (static_cast<closure_t *> (" << closure_generic ().name () << "));";
  return b;
}

str
tame_fn_t::reenter_fn () const
{
  strbuf b;
  b << closure ().type ().base_type ()
    << "::reenter";
  return b;
}

str
tame_fn_t::frozen_arg (const str &i) const
{
  strbuf b;
  b << closure_nm () << "->_args." << i ;
  return b;
}

void
vartab_t::declarations (strbuf &b, const str &padding) const
{
  for (u_int i = 0; i < size (); i++) {
    b << padding << _vars[i].decl () << ";\n";
  }
}

void
vartab_t::initialize (strbuf &b, bool self) const
{
  bool first = true;
  for (u_int i = 0; i < size (); i++) {
    if (self || _vars[i].initializer ()) {
      if (!first) b << ", ";
      first = false;
      b << _vars[i].name () << " (";
      if (self) {
	b << _vars[i].name ();
      } else {
	b << _vars[i].initializer ();
      }
      b << ")";
    }
  }
}

void
vartab_t::paramlist (strbuf &b, bool types) const
{
  for (u_int i = 0; i < size () ; i++) {
    if (i != 0) b << ", ";
    if (types) {
      b << _vars[i].decl ();
    } else {
      b << _vars[i].name ();
    }
  }
}

str
tame_fn_t::label (u_int id) const
{
  strbuf b;
  b << _name_mangled << "__label" << id ;
  return b;
}


//
//
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// Output Routines

void 
tame_passthrough_t::output (int fd)
{
  if (_strs.size ()) 
    state.output_line_xlate (fd, _strs[0].lineno ());
  _buf.tosuio ()->output (fd);
}

str
tame_nonblock_callback_t::cb_name () const
{
  strbuf b;
  u_int N_w = _nonblock->n_args ();
  u_int N_p = _call_with->size ();
  b << "__nonblock_cb_" << N_w << "_" << N_p;
  return b;
}

void
tame_nonblock_callback_t::output_generic (strbuf &b)
{
  u_int N_w = _nonblock->n_args ();
  u_int N_p = _call_with->size ();

  if (generic_cb_exists (N_w, N_p))
    return;

  generic_cb_declare (N_w, N_p);
  b << "template<class J";
  for (u_int i = 1; i <= N_p; i++) {
    b << ", class P" << i;
  }
  for (u_int i = 1; i <= N_w; i++) {
    b << ", class W" << i;
  }
  b << "> static void\n";
  b.cat (cb_name ().cstr (), true);
  b << " (ptr<closure_t> hold, J *jg";
  if (N_p) {
    b << ",\n";
    b << "\t\tpointer_set" << N_p << "_t<";
    for (u_int i = 1; i <= N_p; i++) {
      if (i != 1) b << ", ";
      b << "P" << i;
    }
    b << "> p";
  }
  if (N_w) {
    b << ",\n";
    b << "\t\tvalue_set_t<";
    for (u_int i = 1; i <= N_w; i++) {
      if (i != 1) b << ", ";
      b << "W" << i;
    }
    b << "> w";
  }
  if (N_p) {
    b << ",\n\t\t";
    for (u_int i = 1; i <= N_p; i++) {
      if (i != 1) b << ", ";
      b << "P" << i << " v" << i;
    }
  }
  b << ")\n{\n";
  for (u_int i = 1; i <= N_p; i++) {
    b << "  *p.p" << i << " = v" << i << ";\n";
  }
  b << "\n";
  b << "  delaycb (0, 0, wrap (jg, &J::join, w));\n"
    << "}\n\n";
}
  
void 
tame_block_callback_t::output_in_class (strbuf &b)
{
  u_int N_p = _call_with->size ();
  
  if (N_p) {
    b << "  template<";
    for (u_int i = 1; i <= N_p; i++) {
      if (i != 1) b << ", ";
      b << "class T" << i;
    }
    b << ">\n";
  }
  b << "  void cb" << _cb_ind  << " (";

  if (N_p) {
    b << "pointer_set" << N_p << "_t<";
    for (u_int i = 1; i <= N_p; i++) {
      if (i != 1) b << ", ";
      b << "T" << i;
    }
    b << "> p" ;
    for (u_int i = 1; i <= N_p; i++) {
      b << ", T" << i << " v" << i;
    }
  }
  b << ")\n  {\n";
  for (u_int i = 1; i <= N_p; i++) {
    b << "    *p.p" << i << " = v" << i << ";\n";
  }
  b << "    if (!--_block" << _cb_ind << ")\n"
    << "      delaycb (0, 0, wrap (mkref (this), &"
    ;
  b.cat (_parent_fn->reenter_fn ().cstr (), true);
  b << "));\n";
  b << "  }\n\n";
}

void
tame_fn_t::output_reenter (strbuf &b)
{
  b << "  void reenter ()\n"
    << "  {\n"
    ;

  b << "    ";
  if (_class)
    b <<  "_self->";

  b << _method_name << " (";

  for (u_int i = 0; _args && i < _args->_vars.size (); i++) {
    b << "_args." << _args->_vars[i].name ();
    b << ", ";
  }
  b << "mkref (this));\n"
    << "  }\n\n";
}

void
tame_fn_t::output_closure (int fd)
{
  my_strbuf_t b;

  b << "class " << _closure.type ().base_type () 
    << " : public closure_t "
    << "{\n"
    << "public:\n"
    << "  " << _closure.type ().base_type () 
    << " (";

  if (_class) {
    b.cat (_self.decl (), true);
    if (_args)
      b << ", ";
  }

  if (_args) {
    _args->paramlist (b);
  }

  b << ") : ";
  if (_class) {
    str s = _self.name ();
    b.mycat (s) << " (";
    b.mycat (s) << "), ";
  }

  b << " _stack ("
    ;
  if (_args) _args->paramlist (b, false);
  b << "), _args ("
    ;

  if (_args) _args->paramlist (b, false);
  b << ")";

  for ( u_int i = 1; i <= _cbs.size (); i++) {
    b << ", _block" << i << " (0)";
  }
  b << " {}\n\n";

  for (u_int i = 0; i < _cbs.size (); i++) {
    _cbs[i]->output_in_class (b);
  }

  output_reenter (b);


  // output the stack structure
  b << "  struct stack_t {\n"
    << "    stack_t (";
  if (_args) _args->paramlist (b, true);
  b << ")" ;

  // output stack declaration
  if (_stack_vars.size ()) {
    strbuf i;
    _stack_vars.initialize (i, false);
    str s (i);
    if (s && s.len () > 0) {
      b << " : " << s << " ";
    }
  }

  b << " {}\n";
    ;
  _stack_vars.declarations (b, "    ");
  b << "  };\n";
 
  // output the argument capture structure
  b << "\n"
    << "  struct args_t {\n"
    << "    args_t (" ;
  if (_args && _args->size ()) 
    _args->paramlist (b, true);
  b << ")";
  if (_args && _args->size ()) {
    b << " : ";
    _args->initialize (b, true);
  }
  b << " {}\n";
  if (_args)  _args->declarations (b, "    ");
  b << "  };\n";

  if (_class) {
    b.mycat (_self.decl ()) << ";\n";
  }
  b << "  stack_t _stack;\n"
    << "  args_t _args;\n" ;

  for (u_int i = 1; i <= _cbs.size (); i++) {
    b << "  int _block" << i << ";\n";
  }

  b << "};\n\n";

  b.tosuio ()->output (fd);
}

void
tame_fn_t::output_stack_vars (strbuf &b)
{
  for (u_int i = 0; i < _stack_vars.size (); i++) {
    const var_t &v = _stack_vars._vars[i];
    b << "  " << v.ref_decl () << " = " 
      << closure_nm () << "->_stack." << v.name () << ";\n" ;
  } 
}

void
tame_fn_t::output_jump_tab (strbuf &b)
{
  b << "  switch (" << CLOSURE << "->jumpto ()) {\n"
    ;
  for (u_int i = 0; i < _envs.size (); i++) {
    if (_envs[i]->is_jumpto ()) {
      int id_tmp = _envs[i]->id ();
      assert (id_tmp);
      b << "  case " << id_tmp << ":\n"
	<< "    goto " << label (id_tmp) << ";\n"
	<< "    break;\n";
    }
  }
  b << "  default:\n"
    << "    break;\n"
    << "  }\n";
}

str
tame_fn_t::signature (bool d) const
{
  strbuf b;
  b << _ret_type.to_str () << "\n"
    << _name << "(";
  if (_args) {
    _args->paramlist (b);
    b << ", ";
  }
  b << closure_generic ().decl ();
  if (d)
    b << " = NULL";
  b << ")";

  return b;
}

void
tame_fn_t::output_static_decl (int fd)
{
  strbuf b;
  b << "static " << signature (true) << ";\n\n";
  
  b.tosuio ()->output (fd);
}

void
tame_fn_t::output_fn (int fd)
{
  my_strbuf_t b;
  
  state.need_line_xlate ();
  state.output_line_xlate (fd, _lineno);

  b << signature (false)  << "\n"
    << "{\n"
    << "  " << _closure.decl () << ";\n"
    << "  "
    ;
  b.mycat (_closure.type ().mk_ptr ());
  b << " " << CLOSURE_RFCNT << ";\n"
    << "  if (!" << closure_generic ().name() << ") {\n"
    ;

  strbuf dat;

  b << "    " << CLOSURE_RFCNT << " = New refcounted<"
    << _closure.type().base_type () << "> (";

  if (_class) {
    b << "this";
    if (_args)
      b << ", ";
  }

  if (_args)
    _args->paramlist (b, false);
  b << ");\n"
    << "    " << CLOSURE << " = " << CLOSURE_RFCNT << ";\n"
    << "    " << CLOSURE_GENERIC << " = " << CLOSURE_RFCNT << ";\n"
    << "  } else {\n"
    << "    " << _closure.name () << " = " << decl_casted_closure (false)
    << "\n"
    << "    " << CLOSURE_RFCNT << " = mkref (" << CLOSURE << ");\n"
    << "  }\n\n"
    ;

  output_stack_vars (b);
  b << "\n";

  output_jump_tab (b);

  b.tosuio ()->output (fd);

  state.need_line_xlate ();

  element_list_t::output (fd);

}

void 
tame_fn_t::output (int fd)
{
  if (_opts & STATIC_DECL)
    output_static_decl (fd);
  output_generic (fd);
  output_closure (fd);
  output_fn (fd);
}

void
tame_fn_t::output_generic (int fd)
{
  strbuf b;
  for (u_int i = 0; i < _nbcbs.size (); i++) {
    _nbcbs[i]->output_generic (b);
  }
  b.tosuio ()->output (fd);
}

void
tame_fn_t::jump_out (strbuf &b, int id)
{
  for (u_int i = 0; i < args ()->size (); i++) {
    b << "    " <<  CLOSURE << "->_args." << args ()->_vars[i].name ()
      << " = " << args ()->_vars[i].name () << ";\n";
  }

  b << "    " << CLOSURE << "->set_jumpto (" << id 
    << ");\n"
    << "\n";
}

void 
tame_block_t::output (int fd)
{
  my_strbuf_t b;
  str tmp;

  b << "  {\n"
    << "    " << CLOSURE << "->_block" << _id << " = 1;\n"
    ;

  _fn->jump_out (b, _id);

  b.tosuio ()->output (fd);
  b.tosuio ()->clear ();

  // now we are returning to mainly pass-through code, but with some
  // callbacks thrown in (which won't change the line-spacing)
  state.need_line_xlate ();

  for (tame_el_t *el = _lst.first; el; el = _lst.next (el)) {
    el->output (fd);
  }

  b << "\n"
    << "    if (--" << CLOSURE << "->_block" << _id << ")\n"
    << "      return;\n"
    << "  }\n"
    << " " << _fn->label (_id) << ":\n"
    ;

  state.need_line_xlate ();
  b.tosuio ()->output (fd);
}

void
parse_state_t::output (int fd)
{
  output_line_xlate (fd, 1);
  element_list_t::output (fd);
}

void
expr_list_t::output_vars (strbuf &b, bool first, const str &prfx, 
			  const str &sffx)
{
  for (u_int i = 0; i < size (); i++) {
    if (!first) b << ", ";
    else first = false;
    if (prfx) b << prfx;
    b << (*this)[i].name ();
    if (sffx) b << sffx;
  }
}

void
tame_nonblock_t::output_vars (strbuf &b, bool first, const str &prfx,
				const str &sffx)
{
  for (u_int i = 0; i < n_args (); i++) {
    if (!first)  b << ", ";
    else first = false;
    if (prfx) b << prfx;
    b << arg (i).name ();
    if (sffx) b << sffx;
  }
}

void
tame_block_callback_t::output (int fd)
{
  int bid = _block->id ();
  my_strbuf_t b;
  b << "(++" << CLOSURE << "->_block" << bid << ", "
    << "wrap (" << CLOSURE_RFCNT << ", &" 
    << _parent_fn->closure ().type ().base_type () << "::cb" << _cb_ind
    ;

  if (_call_with->size ()) {
    b << "<";
    _call_with->output_vars (b, true, "typeof (", ")");
    b << ">, pointer_set" << _call_with->size () << "_t<";
    _call_with->output_vars (b, true, "typeof (", ")");
    b << "> (";
    _call_with->output_vars (b, true, "&(", ")");
    b << ")";
  }
  b << "))";

  b.tosuio ()->output (fd);
}

void
tame_nonblock_callback_t::output (int fd)
{
  my_strbuf_t b;

  strbuf tmp;
  tmp << "(" << _nonblock->join_group ().name () << ")";
  str jgn = tmp;
  
  b << "(" << jgn << "->launch_one (), "
    << "wrap (";
  b.mycat (cb_name ()) << "<typeof (*" << jgn << ")";

  _call_with->output_vars (b, false, "typeof (", ")");
  _nonblock->output_vars (b, false, "typeof (", ")");

  b << ">, " << CLOSURE_GENERIC << ", " << jgn;

  if (_call_with->size ()) {
    b << ", pointer_set" << _call_with->size () << "_t<";
    _call_with->output_vars (b, true, "typeof (", ")");
    b << "> (";
    _call_with->output_vars (b, true, "&(", ")");
  }
  b << ")";

  if (_nonblock->n_args ()) {
    b << ", value_set_t<";
    _nonblock->output_vars (b, true, "typeof (", ")");
    b << "> (";
    _nonblock->output_vars (b, true, NULL, NULL);
  }

  b << ")))";

  b.tosuio ()->output (fd);

}

#define JOIN_VALUE "__v"
void
tame_join_t::output (int fd)
{
  my_strbuf_t b;
  b << "  ";
  b.mycat (_fn->label (_id)) << ":\n";
  b << "    typeof ((" << join_group ().name () << ")->to_vs ()) "
    << JOIN_VALUE << ";\n";
  b << "    if ((" <<  join_group ().name () << ")->pending (&" 
    << JOIN_VALUE << ")) {\n";
  
  for (u_int i = 0; i < n_args (); i++) {
    b << "      typeof (" << JOIN_VALUE << ".v" << i+1 << ") &"
      << arg (i).name ()  << " = " JOIN_VALUE << ".v" << i+1 << ";\n";
  }
  b << "\n";
  b.tosuio ()->output (fd);

  b.tosuio ()->clear ();
  state.need_line_xlate ();
  element_list_t::output (fd);

  b << "    } else {\n"
    ;
  
  _fn->jump_out (b, _id);
  
  b << "      (" << join_group ().name ()
    << ")->set_join_cb (wrap (" << CLOSURE_RFCNT
    << ", &" << _fn->reenter_fn () << "));\n"
    << "      return;\n"
    << "  }\n\n";

  b.tosuio ()->output (fd);
}

void
parse_state_t::output_line_xlate (int fd, int ln)
{
  if (_xlate_line_numbers && _need_line_xlate) {
    strbuf b;
    b << "# " << ln << " \"" << _infile_name << "\"\n";
    b.tosuio ()->output (fd);
    _need_line_xlate = false;
  }
}

//
//-----------------------------------------------------------------------