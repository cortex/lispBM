/*
    Copyright 2018 Joel Svensson	svenssonjoel@yahoo.se

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "symrepr.h"
#include "heap.h"
#include "builtin.h"
#include "print.h"
#include "env.h"
#include "bytecode.h"
#include "eval_cps.h"
#include "stack.h"
#include "fundamental.h"
#ifdef VISUALIZE_HEAP
#include "heap_vis.h"
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DONE              1
#define SET_GLOBAL_ENV    2
#define FUNCTION_APP      3
#define FUNCTION          4
#define BIND_TO_KEY_REST  5
#define IF                6
#define ARG_LIST          7
#define EVAL              8
#define PROGN_REST        9

VALUE run_eval(eval_context_t *ctx);

static VALUE eval_cps_global_env;
static VALUE NIL;

eval_context_t *eval_context = NULL;

eval_context_t *eval_cps_get_current_context(void) {
  return eval_context;
}

eval_context_t *eval_cps_new_context_inherit_env(VALUE program, VALUE curr_exp) {
  eval_context_t *ctx = malloc(sizeof(eval_context_t));
  ctx->program = program;
  ctx->curr_exp = curr_exp;
  ctx->curr_env = eval_context->curr_env;
  ctx->K = stack_init(100, true);
  ctx->next = eval_context;
  eval_context = ctx;
  return ctx;
}

void eval_cps_drop_top_context(void) {
  eval_context_t *ctx = eval_context;
  eval_context = eval_context->next;
  stack_del(ctx->K);
  free(ctx);
}

VALUE eval_cps_get_env(void) {
  return eval_cps_global_env;
}

VALUE eval_cps_bi_eval(VALUE exp) {
  eval_context_t *ctx = eval_cps_get_current_context();

  ctx->curr_exp = exp;
  return run_eval(ctx);
}

VALUE eval_cps_bi_byte_comp(VALUE arg_list) {

  (void) arg_list;
  /*
  VALUE exp = car(arg_list); // todo check for error
  int err;
  
  eval_context_t *ctx = eval_cps_get_current_context();
  bytecode_t *bc = malloc(sizeof(bytecode_t));
  if (!bytecode_create(bc, 4096)) {
    return enc_sym(symrepr_eerror());
  }
  if (!bytecode_compile(ctx->K, exp, bc, &err)) {
    bytecode_del(bc);
    return enc_sym(symrepr_eerror());
  }

  VALUE res = cons((UINT)bc,enc_sym(SPECIAL_SYM_BYTECODE));
  if (is_ptr(res)) {
    res = set_ptr_type(res, PTR_TYPE_BYTECODE);
  }
  
  return res;
  */
  return enc_sym(symrepr_nil());
}

// ////////////////////////////////////////////////////////
// Continuation points and apply cont
// ////////////////////////////////////////////////////////

VALUE cont_set_global_env(eval_context_t *ctx, VALUE val, bool *perform_gc){

  VALUE curr = eval_cps_global_env;
  VALUE tmp;
  VALUE key;
  pop_u32(ctx->K, &key);

  while(type_of(curr) == PTR_TYPE_CONS) {
    if (car(car(curr)) == key) {
      set_cdr(car(curr),val);
      return enc_sym(symrepr_true());
    }
    curr = cdr(curr);
  }

  VALUE keyval;
  keyval = cons(key,val);
  if (type_of(keyval) == VAL_TYPE_SYMBOL) {
    push_u32_2(ctx->K, key, enc_u(SET_GLOBAL_ENV));
    *perform_gc = true;
    return val;
  }

  tmp = cons(keyval, eval_cps_global_env);
  if (type_of(tmp) == VAL_TYPE_SYMBOL) {
    push_u32_2(ctx->K, key, enc_u(SET_GLOBAL_ENV));
    *perform_gc = true;
    return val;
  }

  eval_cps_global_env = tmp;
  return enc_sym(symrepr_true());
}

VALUE apply_continuation(eval_context_t *ctx, VALUE arg, bool *done, bool *perform_gc, bool *app_cont){

  VALUE k;
  pop_u32(ctx->K, &k);

  VALUE res;

  *app_cont = false;

  switch(dec_u(k)) {
  case DONE:
    *done = true;
    return arg;
  case EVAL:
    ctx->curr_exp = arg;
    return enc_u(0);
  case SET_GLOBAL_ENV:
    res = cont_set_global_env(ctx, arg, perform_gc);
    *app_cont = true;
    return res;
  case PROGN_REST: {
    VALUE rest;
    pop_u32(ctx->K, &rest);
    if (type_of(rest) == VAL_TYPE_SYMBOL && rest == NIL) {
      res = arg;
      *app_cont = true;
      return res;
    }

    if (symrepr_is_error(rest)) {
      res = rest;
      *done = true;
      return res;
    }

    push_u32_2(ctx->K, cdr(rest), enc_u(PROGN_REST));
    ctx->curr_exp = car(rest);
    return enc_u(0);
  } 
  case FUNCTION_APP: {
    VALUE args;
    VALUE args_rev;
    VALUE fun = arg;

    pop_u32(ctx->K, &args);

    if (type_of(args) == PTR_TYPE_CONS) { // TODO: FIX THIS MESS
      args_rev = reverse(args);
      if (type_of(args_rev) == VAL_TYPE_SYMBOL) {
	push_u32_2(ctx->K, args, enc_u(FUNCTION_APP));
	*perform_gc = true;
	*app_cont = true;
	return fun;
      }
    } else {
      args_rev = args;
    }

    if (type_of(fun) == PTR_TYPE_CONS) { // its a closure
       VALUE params  = car(cdr(fun));
       VALUE exp     = car(cdr(cdr(fun)));
       VALUE clo_env = car(cdr(cdr(cdr(fun))));

       VALUE local_env;
       if (length(params) != length(args)) { // programmer error
	 printf("Length mismatch params - args\n");
	 simple_print(params); printf("\n");
	 simple_print(args); printf("\n");
	 *done = true;
	 return enc_sym(symrepr_eerror());
       }

       if (!env_build_params_args(params, args_rev, clo_env, &local_env)) {
	 push_u32_2(ctx->K, args, enc_u(FUNCTION_APP));
	 *perform_gc = true;
	 *app_cont = true;
	 return fun;
       }
       ctx->curr_exp = exp;
       ctx->curr_env = local_env;
       return 0;

    } else if (type_of(fun) == PTR_TYPE_BYTECODE) {
      eval_context_t *ctx = eval_cps_get_current_context();

      VALUE curr_arg = args_rev;

      while (curr_arg != NIL) {
	push_u32(ctx->K, car(curr_arg));
      }
      if (type_of(fun) == PTR_TYPE_BYTECODE) {
	bytecode_t *bc = (bytecode_t*)car(fun);
	res = bytecode_eval(ctx->K, bc);
	*app_cont = true;
	return res;
      }
      else return enc_sym(symrepr_eerror());
      //TODO: Return stack to same state as before running bc, unless
      //      it is expected that running the bc keeps exits with stack in good shape.
    }else if (type_of(fun) == VAL_TYPE_SYMBOL) { // its a fundamental operation

      VALUE curr_arg = args;

      UINT nargs = 0;
      VALUE res;
      while (type_of(curr_arg) == PTR_TYPE_CONS) {
	push_u32(ctx->K, car(curr_arg));
	nargs++;
	curr_arg = cdr(curr_arg);
      }
      push_u32(ctx->K, enc_u(nargs));
      if (!fundamental_exec(ctx->K, fun)) {

	*done = true;
	return enc_sym(symrepr_merror());
      } else {
	pop_u32(ctx->K, &res);
	if (type_of(res) == VAL_TYPE_SYMBOL &&
	    dec_sym(res) == symrepr_merror()) {
	  push_u32_2(ctx->K, args, enc_u(FUNCTION_APP));
	  *perform_gc = true;
	  *app_cont = true;
	  return fun;
	}
	*app_cont = true;
	return res;
      }
      
      /*
      VALUE (*f)(VALUE) = builtin_lookup_function(dec_sym(fun));

      if (f == NULL) {
	*done = true;
	return enc_sym(symrepr_eerror());
      }
      VALUE f_res = f(args_rev);
      
      if (type_of(f_res) == VAL_TYPE_SYMBOL &&
	  (dec_sym(f_res) == symrepr_merror())) {
	push_u32_2(ctx->K, args, enc_u(FUNCTION_APP));
	*perform_gc = true;
	*app_cont = true;
	return fun;
      }
      */
      *app_cont = true;
      return res;
    }
  } break;
  case ARG_LIST: {
    VALUE rest;
    VALUE acc;
    VALUE env;
    pop_u32_3(ctx->K, &rest, &acc, &env);
    VALUE acc_ = cons(arg, acc);
    if (type_of(acc_) == VAL_TYPE_SYMBOL) {
      push_u32_4(ctx->K, env, acc, rest, enc_u(ARG_LIST));
      *perform_gc = true;
      *app_cont   = true;
      return arg;            // RESET EXECUTION
    }
    if (type_of(rest) == VAL_TYPE_SYMBOL &&
	rest == NIL) {
      *app_cont = true;
      return acc_;
    }
    VALUE head = car(rest);
    push_u32_4(ctx->K, env, acc_, cdr(rest), enc_u(ARG_LIST));
    ctx->curr_env = env;
    ctx->curr_exp = head;
    return enc_u(0);
  }
  case FUNCTION: {
    VALUE fun;
    pop_u32(ctx->K, &fun);
    push_u32_2(ctx->K, arg, enc_u(FUNCTION_APP));
    if (is_fundamental(fun)) {
      *app_cont = true;
      return fun;
    } 
    ctx->curr_exp = fun;
    return enc_u(0); // Should return something that is very easy to recognize as nonsense 
  }
  case BIND_TO_KEY_REST:{
    VALUE key;
    VALUE env;
    VALUE rest;

    pop_u32_3(ctx->K, &key, &env, &rest);

    env_modify_binding(env, key, arg);

    if ( type_of(rest) == PTR_TYPE_CONS ){
      VALUE keyn = car(car(rest));
      VALUE valn_exp = car(cdr(car(rest)));

      push_u32_4(ctx->K, cdr(rest), env, keyn, enc_u(BIND_TO_KEY_REST));

      ctx->curr_exp = valn_exp;
      ctx->curr_env = env;
      return enc_u(0);
    }

    // Otherwise evaluate the expression in the populated env
    VALUE exp;
    pop_u32(ctx->K, &exp);
    ctx->curr_exp = exp;
    ctx->curr_env = env;
    return enc_u(0);
  }
  case IF: {
    VALUE then_branch;
    VALUE else_branch;

    pop_u32_2(ctx->K, &then_branch, &else_branch);

    if (type_of(arg) == VAL_TYPE_SYMBOL && dec_sym(arg) == symrepr_true()) {
      ctx->curr_exp = then_branch;
    } else {
      ctx->curr_exp = else_branch;
    }
    return 0;
  }
  } // end switch
  *done = true;
  return enc_sym(symrepr_eerror());
}

VALUE run_eval(eval_context_t *ctx){

  push_u32(ctx->K, enc_u(DONE));

  VALUE r = NIL;
  bool done = false;
  bool perform_gc = false;
  bool app_cont = false;

  uint32_t non_gc = 0;

  while (!done) {

#ifdef VISUALIZE_HEAP
    heap_vis_gen_image();
#endif
    
    if (perform_gc) {
      if (non_gc == 0) {
	done = true;
	r = enc_sym(symrepr_merror());
	continue;
      }
      non_gc = 0; 
      heap_perform_gc_aux(eval_cps_global_env,
			  ctx->curr_env,
			  ctx->curr_exp,
			  ctx->program,
			  r,
			  ctx->K->data,
			  ctx->K->sp);
      perform_gc = false;
    } else {
      non_gc ++;
    }

    if (app_cont) {
      r = apply_continuation(ctx, r, &done, &perform_gc, &app_cont);
      continue;
    }
    app_cont = false;

    VALUE head;
    VALUE value = enc_sym(symrepr_eerror());

    switch (type_of(ctx->curr_exp)) {

    case VAL_TYPE_SYMBOL:
      if (!env_lookup(ctx->curr_exp, ctx->curr_env, &value)) {
	if (!env_lookup(ctx->curr_exp, eval_cps_global_env, &value)){
	  r = enc_sym(symrepr_eerror());
	  done = true;
	  continue;
	}
      }
      app_cont = true;
      r = value;
      break;
    case PTR_TYPE_BOXED_F:
    case PTR_TYPE_BOXED_U:
    case PTR_TYPE_BOXED_I:
    case VAL_TYPE_I:
    case VAL_TYPE_U:
    case VAL_TYPE_CHAR:
    case PTR_TYPE_ARRAY:
      app_cont = true;
      r = ctx->curr_exp;
      break;
    case PTR_TYPE_REF:
    case PTR_TYPE_STREAM:
      r = enc_sym(symrepr_eerror());
      done = true;
      break;
    case PTR_TYPE_CONS:
      head = car(ctx->curr_exp);

      if (type_of(head) == VAL_TYPE_SYMBOL) {

	// Special form: QUOTE
	if (dec_sym(head) == symrepr_quote()) {
	  value = car(cdr(ctx->curr_exp));
	  app_cont = true;
	  r = value;
	  continue;
	}

	// Special form: DEFINE
	if (dec_sym(head) == symrepr_define()) {
	  VALUE key = car(cdr(ctx->curr_exp));
	  VALUE val_exp = car(cdr(cdr(ctx->curr_exp)));

	  if (type_of(key) != VAL_TYPE_SYMBOL ||
	      key == NIL) {
	    done = true;
	    r =  enc_sym(symrepr_eerror());
	    continue;
	  }

	  push_u32_2(ctx->K, key, enc_u(SET_GLOBAL_ENV));
	  ctx->curr_exp = val_exp;
	  continue;
	}

	// Special form: PROGN
	if (dec_sym(head) == symrepr_progn()) {
	  VALUE exps = cdr(ctx->curr_exp);

	  if (type_of(exps) == VAL_TYPE_SYMBOL && exps == NIL) {
	    r = enc_sym(symrepr_nil());
	    app_cont = true;
	    continue;
	  }

	  if (symrepr_is_error(exps)) {
	    r = exps;
	    done = true;
	    continue;
	  }
	  push_u32_2(ctx->K, cdr(exps), enc_u(PROGN_REST));
	  ctx->curr_exp = car(exps);
	  continue;
	}

	// Special form: LAMBDA
	if (dec_sym(head) == symrepr_lambda()) {
	  VALUE env_cpy;

	  if (!env_copy_shallow(ctx->curr_env, &env_cpy)) {
	    perform_gc = true;
	    app_cont = false;
	    continue; // perform gc and resume evaluation at same expression
	  }

	  VALUE env_end;
	  VALUE body;
	  VALUE params;
	  VALUE closure;
	  env_end = cons(env_cpy,NIL);
	  body    = cons(car(cdr(cdr(ctx->curr_exp))), env_end);
	  params  = cons(car(cdr(ctx->curr_exp)), body);
	  closure = cons(enc_sym(symrepr_closure()), params);

	  if (type_of(env_end) == VAL_TYPE_SYMBOL ||
	      type_of(body)    == VAL_TYPE_SYMBOL ||
	      type_of(params)  == VAL_TYPE_SYMBOL ||
	      type_of(closure) == VAL_TYPE_SYMBOL) {
	    perform_gc = true;
	    app_cont = false;
	    continue; // perform gc and resume evaluation at same expression
	  }

	  app_cont = true;
	  r = closure;
	  continue;
	}

	// Special form: IF
	if (dec_sym(head) == symrepr_if()) {

	  push_u32_3(ctx->K,
		     car(cdr(cdr(cdr(ctx->curr_exp)))), // Else branch
		     car(cdr(cdr(ctx->curr_exp))),      // Then branch
		     enc_u(IF));
	  ctx->curr_exp = car(cdr(ctx->curr_exp));
	  continue;
	}
	// Special form: LET
	if (dec_sym(head) == symrepr_let()) {
	  VALUE orig_env = ctx->curr_env;
	  VALUE binds    = car(cdr(ctx->curr_exp)); // key value pairs.
	  VALUE exp      = car(cdr(cdr(ctx->curr_exp))); // exp to evaluate in the new env.

	  VALUE curr = binds;
	  VALUE new_env = orig_env;

	  if (type_of(binds) != PTR_TYPE_CONS) {
	    // binds better be nil or there is a programmer error.
	    ctx->curr_exp = exp;
	    continue;
	  }

	  // Implements letrec by "preallocating" the key parts
	  while (type_of(curr) == PTR_TYPE_CONS) {
	    VALUE key = car(car(curr));
	    VALUE val = NIL;
	    VALUE binding;
	    binding = cons(key, val);
	    new_env = cons(binding, new_env);

	    if (type_of(binding) == VAL_TYPE_SYMBOL ||
		type_of(new_env) == VAL_TYPE_SYMBOL) {
	      perform_gc = true;
	      app_cont = false;
	      continue;
	    }
	    curr = cdr(curr);
	  }

	  VALUE key0 = car(car(binds));
	  VALUE val0_exp = car(cdr(car(binds)));

	  push_u32_5(ctx->K, exp, cdr(binds), new_env,
		     key0, enc_u(BIND_TO_KEY_REST));
	  ctx->curr_exp = val0_exp;
	  ctx->curr_env = new_env;
	  continue;
	}
      } // If head is symbol

      push_u32_2(ctx->K, head, enc_u(FUNCTION));
      if (type_of(cdr(ctx->curr_exp)) == VAL_TYPE_SYMBOL &&
	  cdr(ctx->curr_exp) == NIL) {
	// no arguments)
	app_cont = true;
	r = NIL;
	continue;
      } else {
	push_u32_4(ctx->K, ctx->curr_env, NIL,
		   cdr(cdr(ctx->curr_exp)), enc_u(ARG_LIST));

	ctx->curr_exp = car(cdr(ctx->curr_exp));;
	continue;
      }
    default:
      // BUG No applicable case!
      done = true;
      r = enc_sym(symrepr_eerror());
      break;
    }
  } // while (!done)
  return r;
}

VALUE eval_cps_program(VALUE lisp) {

  eval_context_t *ctx = eval_cps_get_current_context();

  ctx->program  = lisp;
  VALUE res = NIL;
  VALUE curr = lisp;

  if (symrepr_is_error(dec_sym(lisp))) return lisp;

  while (type_of(curr) == PTR_TYPE_CONS) {
    if (ctx->K->sp > 0) {
      stack_clear(ctx->K); // clear stack if garbage left from failed previous evaluation
    }
    ctx->curr_exp = car(curr);
    ctx->curr_env = NIL;
    res =  run_eval(ctx);
    curr = cdr(curr);
  }
  return res;
}

int eval_cps_init(bool grow_continuation_stack) {
  int res = 1;
  NIL = enc_sym(symrepr_nil());

  eval_cps_global_env = NIL;

  //if (!builtin_add_function("byte-compile", eval_cps_bi_byte_comp)) return 0;
  //if (!builtin_add_function("eval", eval_cps_bi_eval)) return 0;
  //eval_cps_global_env = built_in_gen_env();

  eval_context = (eval_context_t*)malloc(sizeof(eval_context_t));

  eval_context->K = stack_init(100, grow_continuation_stack);

  VALUE nil_entry = cons(NIL, NIL);
  eval_cps_global_env = cons(nil_entry, eval_cps_global_env);

  if (type_of(nil_entry) == VAL_TYPE_SYMBOL ||
      type_of(eval_cps_global_env) == VAL_TYPE_SYMBOL) res = 0;

  return res;
}

void eval_cps_del(void) {
  stack_del(eval_context->K);
  free(eval_context);
}
