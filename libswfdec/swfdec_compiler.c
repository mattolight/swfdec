#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <js/jsapi.h>
#include <js/jsarena.h>
#include <js/jsatom.h>
#include <js/jsemit.h>
#include <js/jsscript.h>
#include <js/jsopcode.h>

#include "swfdec_bits.h"
#include "swfdec_compiler.h"
#include "swfdec_debug.h"
#include "swfdec_player_internal.h"
#include "swfdec_sprite.h"

/*** NOTE TO HACKERS ***

  This file is supposed to contain a compiler that compiles ActionScript code
  as found in an swf file to Javascript bytecode ready to be interpreted by 
  the Spidermonkey engine.
  The problem here is that I have not much of a clue on how to do such a thing 
  right, the SpiderMonkey docs are very nice for users but unfortunately not for 
  people mocking with bytecode. I guess the contents of this file reflects that. 
  So if you can do better, this file has just one public function: 
  swfdec_compile (). Feel free to reimplement it in a better way.

 *** END NOTE TO HACKERS ***/

/*** COMPILE STATE ***/

typedef struct {
  guint			bytecode;	/* offset into bytecode where jump address goes */
  gboolean		extended;	/* if this is an extended jump */
  gboolean		use_bytes;	/* if TRUE, the offset is in bytes, otherwise it's in actions */
  guint			offset;		/* action to jump to */
} Jump;

typedef struct {
  guint			original;	/* amount of bytes we've advanced in the input */
  guint			compiled;	/* amount of bytes we've advancecd in the bytecode */
} Offset;

typedef struct {
  JSContext *		cx;		/* JSContext from player */
  JSAtomList		atoms;		/* accumulated atoms */

  SwfdecBits *		bits;		/* where to read the code from */
  int			version;	/* Flash version we're compiling for */
  GByteArray *		bytecode;	/* generated bytecode so far */
  char *		error;		/* error encountered while compiling */
  GArray *		offsets;	/* offsets of actions */
  GArray *		jumps;		/* accumulated jumps */
  GArray *		trynotes;	/* try/catch blocks */
  GPtrArray *		pool;		/* ConstantPool data */
} CompileState;

static void
compile_state_error (CompileState *state, char *format, ...) G_GNUC_PRINTF (2, 3);
static void
compile_state_error (CompileState *state, char *format, ...)
{
  va_list args;

  g_assert (state->error == NULL);

  va_start (args, format);
  state->error = g_strdup_vprintf (format, args);
  va_end (args);
}

/*** ACTION COMPILATION FUNCTIONS ***/

static void
compile_state_push_offset (CompileState *state, guint command_len)
{
  Offset offset = { command_len, state->bytecode->len };
  if (state->offsets->len > 0)
    offset.original += g_array_index (state->offsets, Offset, state->offsets->len - 1).original;
  g_array_append_val (state->offsets, offset);
}

static jsatomid
atomize_string (CompileState *state, const char *name)
{
  JSAtom *atom;
  JSAtomListElement *ale;

  atom = js_Atomize (state->cx, name, strlen (name), 0);
  ale = js_IndexAtom (state->cx, atom, &state->atoms);
  if (ale == NULL) {
    compile_state_error (state, "Failed to add name %s", name);
    return 0;
  }
  return ALE_INDEX (ale);
}

static jsatomid
atomize_double (CompileState *state, jsdouble d)
{
  JSAtom *atom;
  JSAtomListElement *ale;

  atom = js_AtomizeDouble (state->cx, d, 0);
  ale = js_IndexAtom (state->cx, atom, &state->atoms);
  if (ale == NULL) {
    compile_state_error (state, "Failed to add double %g", d);
    return 0;
  }
  return ALE_INDEX (ale);
}
    
static jsatomid
atomize_int32 (CompileState *state, int i)
{
  JSAtom *atom;
  JSAtomListElement *ale;

  g_assert (i >= G_MININT32 && i <= G_MAXINT32);
  atom = js_AtomizeInt (state->cx, i, 0);
  ale = js_IndexAtom (state->cx, atom, &state->atoms);
  if (ale == NULL) {
    compile_state_error (state, "Failed to add int %d", i);
    return 0;
  }
  return ALE_INDEX (ale);
}
    
#define ONELINER(state, opcode) G_STMT_START { \
  guint8 command = opcode; \
  compile_state_add_code (state, &command, 1); \
} G_STMT_END
#define THREELINER(state, opcode, id1, id2) G_STMT_START { \
  guint8 command[3] = { opcode, id1, id2 }; \
  compile_state_add_code (state, command, 3); \
} G_STMT_END
#define THREELINER_INT(state, opcode, _int) THREELINER (state, opcode, _int >> 8, _int)
#define THREELINER_ATOM(state, opcode, str) G_STMT_START { \
  jsatomid id; \
  id = atomize_string (state, str); \
  THREELINER_INT (state, opcode, id); \
} G_STMT_END

#define PUSH_OBJ(state) ONELINER (state, JSOP_PUSHOBJ)
#define POP(state) ONELINER (state, JSOP_POP)
#define GE(state) ONELINER (state, JSOP_GE)
#define THIS(state) ONELINER (state, JSOP_THIS)
#define SWAP(state) ONELINER (state, JSOP_SWAP)
#define DUP(state) ONELINER (state, JSOP_DUP)
#define FLASHCALL(state) ONELINER (state, JSOP_FLASHCALL)
#define FLASHSWAP(state, n) THREELINER_INT (state, JSOP_FLASHSWAP, n)

#define DO_JUMP(state, opcode, offset) G_STMT_START {\
  guint8 command[3] = { opcode, 0, 0 }; \
  compile_state_add_bytes_jump (state, offset, FALSE); \
  compile_state_add_code (state, command, 3); \
} G_STMT_END
#define IFEQ(state, offset) DO_JUMP (state, JSOP_IFEQ, offset)
#define IFNE(state, offset) DO_JUMP (state, JSOP_IFNE, offset)

static void
compile_state_add_code (CompileState *state, const guint8 *code, guint len)
{
  if (state->error)
    return;
  g_byte_array_append (state->bytecode, code, len);
}

static void
bind_name (CompileState *state, const char *name)
{
  THREELINER_ATOM (state, JSOP_BINDNAME, name);
}

#define TARGET_NAME "__swfdec_target"
static void
compile_state_set_target (CompileState *state)
{
  guint8 command[3] = { JSOP_BINDNAME, 0, 0 };
  compile_state_add_code (state, command, 3);
  SWAP (state);
  command[0] = JSOP_SETNAME;
  compile_state_add_code (state, command, 3);
  POP (state);
}
static void
compile_state_add_target (CompileState *state)
{
  guint8 command[3] = { JSOP_DEFVAR, 0, 0 };
  /* add the TARGET variable */
  if (atomize_string (state, TARGET_NAME) != 0) {
    g_assert_not_reached ();
  }
  compile_state_add_code (state, command, 3);
  THIS (state);
  compile_state_set_target (state);
}

static void
compile_state_init (JSContext *cx, SwfdecBits *bits, int version, CompileState *state)
{
  state->cx = cx;
  state->bits = bits;
  state->version = version;
  ATOM_LIST_INIT (&state->atoms);
  state->bytecode = g_byte_array_new ();
  state->error = NULL;
  state->offsets = g_array_new (FALSE, FALSE, sizeof (Offset));
  state->jumps = g_array_new (FALSE, FALSE, sizeof (Jump));
  state->trynotes = g_array_new (TRUE, FALSE, sizeof (JSTryNote));
  state->pool = g_ptr_array_new ();

  compile_state_add_target (state);
  compile_state_push_offset (state, 0);
}

static void
compile_state_resolve_jumps (CompileState *state)
{
  guint i, j;
  int offset;
  Jump *jump;
  guint8 *bytecode;

  for (i = 0; i < state->jumps->len; i++) {
    jump = &g_array_index (state->jumps, Jump, i);
    bytecode = state->bytecode->data + jump->bytecode + 1;
    if (jump->use_bytes) {
      for (j = 0; j < state->offsets->len; j++) {
	if (g_array_index (state->offsets, Offset, j).original == jump->offset) {
	  offset = g_array_index (state->offsets, Offset, j).compiled; 
	  offset -= jump->bytecode;
	  goto finished;
	}
      }
      compile_state_error (state, "Jumped into action");
      return;
    } else {
      offset = g_array_index (state->offsets, Offset, 
	  MIN (state->offsets->len - 1, jump->offset)).compiled; 
      offset -= jump->bytecode;
    }
finished:
    if (jump->extended) {
      gint32 *data = (gint32 *) bytecode;
      *data = GINT32_TO_BE (offset);
    } else {
      gint16 *data = (gint16 *) bytecode;
      if (offset > G_MAXINT32 || offset < G_MININT32) {
	compile_state_error (state, "jump from %u to %u is too big",
	  jump->bytecode, MIN (state->offsets->len - 1, jump->offset));
	return;
      }
      *data = GINT16_TO_BE (offset);
    }
  }
}

#define OFFSET_MAIN 3
static JSScript *
compile_state_finish (CompileState *state)
{
  JSContext *cx = state->cx;
  JSScript *script = NULL;

  if (state->error == NULL)
    compile_state_resolve_jumps (state);

  if (state->error != NULL) {
    JSAtomMap clear;
    js_InitAtomMap (cx, &clear, &state->atoms);
    js_FreeAtomMap (cx, &clear);
    SWFDEC_ERROR ("%s", state->error);
    g_free (state->error);
    //g_assert_not_reached ();
    goto cleanup;
  }

  script = js_NewScript (cx, state->bytecode->len, 1, state->trynotes->len + 1);
  memcpy (script->code, state->bytecode->data, state->bytecode->len);
  memcpy (script->trynotes, state->trynotes->data, (state->trynotes->len + 1) * sizeof (JSTryNote));
  js_InitAtomMap (cx, &script->atomMap, &state->atoms);
  /* FIXME: figure out a correct value here */
  script->depth = 100;
  script->main = script->code + OFFSET_MAIN;
  SN_MAKE_TERMINATOR (SCRIPT_NOTES (script));

cleanup:
  g_ptr_array_free (state->pool, TRUE);
  g_array_free (state->offsets, TRUE);
  g_array_free (state->jumps, TRUE);
  g_array_free (state->trynotes, TRUE);
  g_byte_array_free (state->bytecode, TRUE);
  return script;
}

static void
add_try_catch_block (CompileState *state, guint n_bytes, guint pc_offset)
{
  ptrdiff_t last = 0;
  ptrdiff_t cur_offset;
  JSTryNote tn;
  
  g_assert (state->bytecode->len >= OFFSET_MAIN);
  cur_offset = state->bytecode->len - OFFSET_MAIN;
  if (state->trynotes->len > 0) {
    JSTryNote *tmp = &g_array_index (state->trynotes, JSTryNote, state->trynotes->len - 1);
    last = tmp->start + tmp->length;
    /* FIXME: allow nesting of try/catch blocks */
    g_assert (last <= cur_offset);
  }
  if (last < cur_offset) {
    /* add "no catching here" note */
    tn.start = last;
    tn.length = cur_offset - last;
    tn.catchStart = 0;
    g_array_append_val (state->trynotes, tn);
  }
  tn.start = cur_offset;
  tn.length = n_bytes;
  tn.catchStart = state->bytecode->len + pc_offset - OFFSET_MAIN;
  SWFDEC_LOG ("adding try/catch at %u (len %u) to offset %u", 
      tn.start, tn.length, tn.catchStart);
  g_array_append_val (state->trynotes, tn);
}

/* NB: n_bytes is relative to the start of this action */
static void
compile_state_add_bytes_jump (CompileState *state, int n_bytes, gboolean extended)
{
  Jump jump = { state->bytecode->len, extended, TRUE, 
    n_bytes + g_array_index (state->offsets, Offset, state->offsets->len - 1).original };

  SWFDEC_LOG ("adding jump to byte %u", jump.offset);
  if (n_bytes < 0 &&
      (guint) -n_bytes > g_array_index (state->offsets, Offset, state->offsets->len - 1).original) {
    compile_state_error (state, "attempting to jump %d bytess backwards at byte %u",
	-n_bytes, g_array_index (state->offsets, Offset, state->offsets->len - 1).original);
    return;
  }
  g_array_append_val (state->jumps, jump);
}

/* must be called before adding the jump command to the bytecode */
static void
compile_state_add_action_jump (CompileState *state, int n_actions, gboolean extended)
{
  Jump jump = { state->bytecode->len, extended, FALSE, state->offsets->len + n_actions };

  if (n_actions < 0 && state->offsets->len < (guint) -n_actions) {
    compile_state_error (state, "attempting to jump %d actions backwards in %u. action",
	-n_actions, state->offsets->len);
    return;
  }
  g_array_append_val (state->jumps, jump);
}

static void
push_target (CompileState *state)
{
  THREELINER (state, JSOP_NAME, 0, 0);
}

static void
push_prop (CompileState *state, const char *name)
{
  THREELINER_ATOM (state, JSOP_GETPROP, name);
}

static void
name (CompileState *state, const char *name)
{
  THREELINER_ATOM (state, JSOP_NAME, name);
}

#if 0
static void
push_prop_without_target (CompileState *state, const char *name)
{
  jsatomid id;
  guint8 command[3];

  id = atomize_string (state, name);
  command[0] = JSOP_BINDNAME;
  command[1] = id >> 8;
  command[2] = id;
  compile_state_add_code (state, command, 3);
  command[0] = JSOP_GETPROP;
  compile_state_add_code (state, command, 3);
}
#endif

static void
push_uint16 (CompileState *state, unsigned int i)
{
  g_assert (i <= G_MAXUINT16);
  SWFDEC_LOG ("pushing %u", i);
  THREELINER_INT (state, JSOP_UINT16, i);
}

static void
read_and_push_uint16 (CompileState *state)
{
  guint i = swfdec_bits_get_u16 (state->bits);
  push_uint16 (state, i);
}

static void
call (CompileState *state, guint n_arguments)
{
  THREELINER_INT (state, JSOP_CALL, n_arguments);
}

static void
call_void_function (CompileState *state, const char *name)
{
  push_prop (state, name);
  PUSH_OBJ (state);
  call (state, 0);
  POP (state);
}

#define DEBUG_TRACE(state) G_STMT_START { \
  ONELINER (state, JSOP_DUP); \
  compile_trace (state, 0, 0); \
}G_STMT_END

static void
compile_trace (CompileState *state, guint action, guint len)
{
  push_uint16 (state, 1);
  bind_name (state, "trace");
  push_prop (state, "trace");
  PUSH_OBJ (state);
  FLASHCALL (state);
  POP (state);
}

static void
compile_get_variable (CompileState *state, guint action, guint len)
{
  push_uint16 (state, 1);
  push_target (state);
  push_prop (state, "eval");
  push_target (state);
  FLASHCALL (state);
}

static void
compile_set_variable (CompileState *state, guint action, guint len)
{
  /* FIXME: handle paths */
  push_target (state);
  FLASHSWAP (state, 3);
  SWAP (state);
  ONELINER (state, JSOP_SETELEM);
  POP (state);
}

static void
push_string (CompileState *state, const char *s)
{
  SWFDEC_LOG ("pushing string: %s", s);
  THREELINER_ATOM (state, JSOP_STRING, s);
}

static void
read_and_push_string (CompileState *state)
{
  const char *s = swfdec_bits_skip_string (state->bits);
  push_string (state, s);
}

static void
read_and_push_float (CompileState *state)
{
  double d = swfdec_bits_get_float (state->bits);
  jsatomid id = atomize_double (state, d);
  
  SWFDEC_LOG ("pushing float: %g", d);
  THREELINER_INT (state, JSOP_NUMBER, id);
}

static void
read_and_push_double (CompileState *state)
{
  double d = swfdec_bits_get_double (state->bits);
  jsatomid id = atomize_double (state, d);

  SWFDEC_LOG ("pushing double: %g", d);
  THREELINER_INT (state, JSOP_NUMBER, id);
}

static void
read_and_push_int32 (CompileState *state)
{
  /* FIXME: spec says U32, do they mean this? */
  gint32 i = swfdec_bits_get_u32 (state->bits);
  jsatomid id = atomize_int32 (state, i);
  SWFDEC_LOG ("pushing int: %d", i);
  THREELINER_INT (state, JSOP_NUMBER, id);
}

static void
compile_push (CompileState *state, guint action, guint len)
{
  SwfdecBits *bits = state->bits;
  guint type;
  unsigned char *end = bits->ptr + len;

  while (bits->ptr < end) {
    type = swfdec_bits_get_u8 (bits);
    SWFDEC_LOG ("push type %u", type);
    switch (type) {
      case 0: /* string */
	read_and_push_string (state);
	break;
      case 1: /* float */
	read_and_push_float (state);
	break;
      case 5: /* boolean */
	type = swfdec_bits_get_u8 (bits);
	if (type)
	  ONELINER (state, JSOP_TRUE);
	else
	  ONELINER (state, JSOP_FALSE);
	break;
      case 6: /* double */
	read_and_push_double (state);
	break;
      case 7: /* 32bit int */
	read_and_push_int32 (state);
	break;
      case 8: /* 8bit ConstantPool address */
	type = swfdec_bits_get_u8 (bits);
	if (type >= state->pool->len) {
	  compile_state_error (state, "Constant pool index %u out of range %u",
	      type, state->pool->len);
	  return;
	}
	push_string (state, (char *) g_ptr_array_index (state->pool, type));
	break;
      case 9: /* 16bit ConstantPool address */
	type = swfdec_bits_get_u16 (bits);
	if (type >= state->pool->len) {
	  compile_state_error (state, "Constant pool index %u out of range %u",
	      type, state->pool->len);
	  return;
	}
	push_string (state, (char *) g_ptr_array_index (state->pool, type));
	break;
      case 4: /* register */
      default:
	compile_state_error (state, "Push: type %u not implemented", type);
	swfdec_bits_getbits (bits, 8 * (end - bits->ptr));
    }
  }
}

static void
compile_goto_label (CompileState *state, guint action, guint len)
{
  push_target (state);
  push_prop (state, "gotoAndStop");
  PUSH_OBJ (state);
  read_and_push_string (state);
  call (state, 1);
  POP (state);
}

static void
compile_goto_frame (CompileState *state, guint action, guint len)
{
  unsigned int i;
  push_target (state);
  push_prop (state, "gotoAndStop");
  PUSH_OBJ (state);
  i = swfdec_bits_get_u16 (state->bits);
  push_uint16 (state, i + 1);
  call (state, 1);
  POP (state);
}

static void
compile_goto_frame2 (CompileState *state, guint action, guint len)
{
  int bias, play;
  if (swfdec_bits_getbits (state->bits, 6)) {
    SWFDEC_WARNING ("reserved bits in GotoFrame2 aren't 0");
  }
  bias = swfdec_bits_getbit (state->bits);
  play = swfdec_bits_getbit (state->bits);
  if (bias) {
    SWFDEC_ERROR ("scene bias not implemented");
  }
  push_uint16 (state, 1);
  push_target (state);
  if (play)
    push_prop (state, "gotoAndPlay");
  else
    push_prop (state, "gotoAndStop");
  PUSH_OBJ (state);
  FLASHCALL (state);
}

static void
compile_wait_for_frame (CompileState *state, guint action, guint len)
{
  guint8 command[3] = { JSOP_IFEQ, 0, 0 };

  push_target (state);
  push_prop (state, "_framesloaded");
  read_and_push_uint16 (state);
  GE (state);
  compile_state_add_action_jump (state, 
      swfdec_bits_get_u8 (state->bits), FALSE);
  compile_state_add_code (state, command, 3);
}

static void
compile_jump (CompileState *state, guint action, guint len)
{
  int amount = swfdec_bits_get_s16 (state->bits);
  DO_JUMP (state, JSOP_GOTO, amount + 5);
}

static void
compile_if (CompileState *state, guint action, guint len)
{
  int amount = swfdec_bits_get_s16 (state->bits);
  /* FIXME: Flash 4 does this differently */
  IFNE (state, amount + 5);
}

static void
compile_set_target (CompileState *state, guint action, guint len)
{
  THIS (state);
  push_prop (state, "eval");
  PUSH_OBJ (state);
  read_and_push_string (state);
  call (state, 1);
  compile_state_set_target (state);
}

static void
compile_set_target_2 (CompileState *state, guint action, guint len)
{
  compile_state_set_target (state);
}

static void
compile_constant_pool (CompileState *state, guint action, guint len)
{
  unsigned int i;
  SwfdecBits *bits = state->bits;

  g_ptr_array_set_size (state->pool, swfdec_bits_get_u16 (bits));
  for (i = 0; i < state->pool->len; i++) {
    g_ptr_array_index (state->pool, i) = (gpointer) swfdec_bits_skip_string (bits);
    if (g_ptr_array_index (state->pool, i) == 0) {
      compile_state_error (state, "Couldn't get string %u/%ufor constant pool",
	  i, state->pool->len);
      return;
    }
  }
}

static void
compile_get_url (CompileState *state, guint action, guint len)
{
  push_target (state);
  push_prop (state, "getURL");
  PUSH_OBJ (state);
  read_and_push_string (state);
  read_and_push_string (state);
  call (state, 2);
  POP (state);
}

static void
compile_oneliner_pop (CompileState *state, guint action, guint len)
{
  JSOp op;
  switch (action) {
    case 0x4f:
      op = JSOP_SETELEM;
      break;
    default:
      g_assert_not_reached ();
      op = JSOP_NOP;
  }
  ONELINER (state, op);
  POP (state);
}

static void
compile_get_member (CompileState *state, guint action, guint len)
{
  add_try_catch_block (state, 1, 4);
  ONELINER (state, JSOP_GETELEM);
  THREELINER_INT (state, JSOP_GOTO, 4);
  POP (state);
}

static void
compile_oneliner (CompileState *state, guint action, guint len)
{
  JSOp op;
  switch (action) {
    case 0x0A:
      op = JSOP_ADD;
      break;
    case 0x0B:
      op = JSOP_SUB;
      break;
    case 0x0C:
      op = JSOP_MUL;
      break;
    case 0x0D:
      op = JSOP_DIV;
      break;
    case 0x12:
      op = JSOP_NOT;
      break;
    case 0x17:
      op = JSOP_POP;
      break;
    case 0x47:
      op = JSOP_ADD;
      break;
    case 0x48:
      op = JSOP_LT;
      break;
    case 0x49:
      op = JSOP_NEW_EQ;
      break;
    case 0x4c:
      op = JSOP_DUP;
      break;
    case 0x67:
      op = JSOP_GT;
      break;
    default:
      g_assert_not_reached ();
      op = JSOP_NOP;
  }
  ONELINER (state, op);
}

static void
compile_call_function (CompileState *state, guint action, guint len)
{
  push_target (state);
  SWAP (state);
  ONELINER (state, JSOP_GETELEM);
  ONELINER (state, JSOP_PUSHOBJ);
  ONELINER (state, JSOP_FLASHCALL);
}

static void
compile_call_method (CompileState *state, guint action, guint len)
{
  add_try_catch_block (state, 1, 6);
  ONELINER (state, JSOP_GETELEM);
  ONELINER (state, JSOP_PUSHOBJ);
#if 0
  /* FIXME: can't add try/catch here, because FLASHCALL removes argument count from the stack (oops) */
  add_try_catch_block (state, 1, 5);
#endif
  ONELINER (state, JSOP_FLASHCALL);
  THREELINER_INT (state, JSOP_GOTO, 17);
  /* exception handling goes here: clean up the stack */
  POP (state);
  POP (state);
  ONELINER (state, JSOP_DUP);
  THREELINER_INT (state, JSOP_IFEQ, 10);
  SWAP (state);
  POP (state);
  ONELINER (state, JSOP_ONE);
  ONELINER (state, JSOP_SUB);
  THREELINER_INT (state, JSOP_GOTO, -8);
  ONELINER (state, JSOP_VOID);
}

static void
compile_start_drag (CompileState *state, guint action, guint len)
{
  guint8 command[3] = { JSOP_IFEQ, 0, 27 };
  /* FIXME: target relative to this or target? */
  push_uint16 (state, 1);
  push_target (state);
  push_prop (state, "eval");
  PUSH_OBJ (state);
  FLASHCALL (state);
  FLASHSWAP (state, 3);
  compile_state_add_code (state, command, 3);
  FLASHSWAP (state, 3);
  FLASHSWAP (state, 6);
  FLASHSWAP (state, 3);
  FLASHSWAP (state, 4);
  FLASHSWAP (state, 5);
  FLASHSWAP (state, 4);
  push_uint16 (state, 5);
  command[0] = JSOP_GOTO;
  command[2] = 6;
  compile_state_add_code (state, command, 3);
  push_uint16 (state, 1);
  SWAP (state);
  FLASHSWAP (state, 3);
  push_prop (state, "startDrag");
  PUSH_OBJ (state);
  FLASHCALL (state);
}

static void
compile_increment (CompileState *state, guint action, guint len)
{
  ONELINER (state, JSOP_ONE);
  ONELINER (state, JSOP_ADD);
}

static void
compile_decrement (CompileState *state, guint action, guint len)
{
  ONELINER (state, JSOP_ONE);
  ONELINER (state, JSOP_SUB);
}

static void
compile_random (CompileState *state, guint action, guint len)
{
  push_uint16 (state, 1);
  bind_name (state, "random");
  push_prop (state, "random");
  PUSH_OBJ (state);
  FLASHCALL (state);
}

static void
compile_get_property (CompileState *state, guint action, guint len)
{
  SWAP (state);
  push_uint16 (state, 2);
  push_target (state);
  push_prop (state, "getProperty");
  PUSH_OBJ (state);
  FLASHCALL (state);
}

static void
compile_set_property (CompileState *state, guint action, guint len)
{
  FLASHSWAP (state, 3);
  push_uint16 (state, 3);
  bind_name (state, "setProperty");
  push_prop (state, "setProperty");
  PUSH_OBJ (state);
  FLASHCALL (state);
  POP (state);
}

static void
compile_string_add (CompileState *state, guint action, guint len)
{
  /* be sure to have strings */
  push_string (state, "");
  ONELINER (state, JSOP_ADD);
  ONELINER (state, JSOP_ADD);
}

static void
compile_equals (CompileState *state, guint action, guint len)
{
  /* ensure we compare numbers */
  ONELINER (state, JSOP_POS);
  ONELINER (state, JSOP_EQ);
  if (state->version <= 4) {
    /* FLash 4 wants 1 or 0 on the stack instead of TRUE or FALSE */
    ONELINER (state, JSOP_ZERO);
    ONELINER (state, JSOP_BITOR);
  }
}

static void
compile_to_integer (CompileState *state, guint action, guint len)
{
  /* There's no opcode so we use a bitwise operation that forces a conversion */
  ONELINER (state, JSOP_ZERO);
  ONELINER (state, JSOP_BITOR);
}

static void
compile_target_path (CompileState *state, guint action, guint len)
{
  ONELINER (state, JSOP_DUP);
  name (state, "MovieClip");
  ONELINER (state, JSOP_INSTANCEOF);
  THREELINER_INT (state, JSOP_IFEQ, 13);
  push_prop (state, "toString");
  PUSH_OBJ (state);
  call (state, 0);
  THREELINER_INT (state, JSOP_GOTO, 4);
  ONELINER (state, JSOP_VOID);
}

static void
compile_new_object (CompileState *state, guint action, guint len)
{
  /* use eval to get at the constructor - spidermonkey doesn't like
   * new with strings */
  push_uint16 (state, 1);
  bind_name (state, "eval");
  push_prop (state, "eval");
  PUSH_OBJ (state);
  FLASHCALL (state);
  /* use call instead of new here - if this doesn't work, a FLASHNEW opcode is needed */
  PUSH_OBJ (state);
  FLASHCALL (state);
}

static void
compile_init_object (CompileState *state, guint action, guint len)
{
  name (state, "Object");
  PUSH_OBJ (state);
  THREELINER_INT (state, JSOP_NEW, 0);
  DUP (state);
  FLASHSWAP (state, 3);
  DUP (state);
  THREELINER_INT (state, JSOP_IFEQ, 17);
  ONELINER (state, JSOP_ONE);
  ONELINER (state, JSOP_SUB);
  FLASHSWAP (state, 5);
  SWAP (state);
  FLASHSWAP (state, 4);
  ONELINER (state, JSOP_SETELEM);
  POP (state);
  THREELINER_INT (state, JSOP_GOTO, -19);
  POP (state);
  POP (state);
}

static void
compile_simple_bind_call (CompileState *state, guint action, guint len)
{
  char *name;
  switch (action) {
    case 0x09:
      name = "stopAllSounds";
      break;
    default:
      g_assert_not_reached ();
      return;
  }
  bind_name (state, name);
  call_void_function (state, name);
}

static void
compile_simple_call (CompileState *state, guint action, guint len)
{
  char *name;

  /* FIXME: shouldn't some functions here PUSH_OBJ instead of push_target? */
  push_target (state);
  switch (action) {
    case 0x04:
      name = "nextFrame";
      break;
    case 0x05:
      name = "prevFrame";
      break;
    case 0x06:
      name = "play";
      break;
    case 0x07:
      name = "stop";
      break;
    case 0x09:
      name = "stopAllSounds";
      break;
    case 0x28:
      name = "stopDrag";
      break;
    default:
      g_assert_not_reached ();
      return;
  }
  call_void_function (state, name);
}

/*** COMPILER ***/

typedef struct {
  guint action;
  const char *name;
  void (* compile) (CompileState *state, guint action, guint len);
} SwfdecActionSpec;

static const SwfdecActionSpec * swfdec_action_find (guint action);

void
swfdec_disassemble (SwfdecPlayer *player, JSScript *script)
{
  guint i;

  for (i = 0; i < script->length; i ++) {
    g_print ("%02X ", script->code[i]);
    if (i % 16 == 15)
      g_print ("\n");
    else if (i % 4 == 3)
      g_print (" ");
  }
  if (i % 16 != 15)
    g_print ("\n");
  js_Disassemble (player->jscx, script, JS_TRUE, stdout);
}

/**
 * swfdec_compile:
 * @player: a #SwfdecPlayer
 * @bits: the data to read
 * @version: ActionScript version to compile for
 *
 * parses the data pointed to by @bits and compiles the ActionScript commands 
 * encountered into a script for later execution.
 *
 * Returns: A new JSScript or NULL on failure
 **/
JSScript *
swfdec_compile (SwfdecPlayer *player, SwfdecBits *bits, int version)
{
  unsigned int action, len;
  const SwfdecActionSpec *current;
  CompileState state;
  JSScript *ret;
#ifndef SWFDEC_DISABLE_DEBUG
  unsigned char *start = bits->ptr;
#endif
#ifndef G_DISABLE_ASSERT
  unsigned char *target;
#endif
//#define SWFDEC_DUMP_SCRIPTS
#ifdef SWFDEC_DUMP_SCRIPTS
  SwfdecBits dump = *bits;
#endif

  g_return_val_if_fail (SWFDEC_IS_PLAYER (player), NULL);
  g_return_val_if_fail (bits != NULL, NULL);

  compile_state_init (player->jscx, bits, version, &state);
  SWFDEC_INFO ("Creating new script in frame");
  while ((action = swfdec_bits_get_u8 (bits))) {
    if (action & 0x80) {
      len = swfdec_bits_get_u16 (bits);
    } else {
      len = 0;
    }
#ifndef G_DISABLE_ASSERT
    target = bits->ptr + len;
#endif
    current = swfdec_action_find (action);
    SWFDEC_DEBUG ("compiling action %d %s (len %d, total %d)", action, 
	current ? current->name : "unknown", len > 0 ? 3 + len : 1,
	bits->ptr - start);
#if 0
    if (state.error == NULL)
      g_print ("  compiling action %d %s (len %d, total %d)\n", action, 
	  current ? current->name : "unknown", len > 0 ? 3 + len : 1,
	  bits->ptr - start);
#endif
    if (state.error == NULL && current && current->compile) {
      current->compile (&state, action, len);
      compile_state_push_offset (&state, len ? 3 + len : 1);
#ifndef G_DISABLE_ASSERT
      if (target != bits->ptr) {
	SWFDEC_ERROR ("parsed length and supposed length differ by %d bytes in %s action",
	    bits->ptr - target, current->name);
      }
      bits->ptr = target;
#endif
    } else {
      swfdec_bits_getbits (bits, len * 8);
      if (state.error == NULL) {
	if (current) {
	  compile_state_error (&state, "No compilation function for %s action", current->name);
	} else {
	  compile_state_error (&state, "unknown action 0x%02X", action);
	}
      }
    }
  }
  ret = compile_state_finish (&state);
#if 0
  if (ret)
    swfdec_disassemble (s, ret);
#endif
#ifdef SWFDEC_DUMP_SCRIPTS
  {
    static int dump_count = 0;
    char *filename = g_strdup_printf ("script%d", dump_count++);
    g_file_set_contents (filename, (char *) dump.ptr, bits->ptr - dump.ptr, NULL);
    g_free (filename);
  }
#endif

  return ret;
}

/* must be sorted! */
SwfdecActionSpec actions[] = {
  /* version 3 */
  { 0x04, "NextFrame", compile_simple_call },
  { 0x05, "PreviousFrame", compile_simple_call },
  { 0x06, "Play", compile_simple_call },
  { 0x07, "Stop", compile_simple_call },
  { 0x08, "ToggleQuality", NULL },
  { 0x09, "StopSounds", compile_simple_bind_call },
  /* version 4 */
  { 0x0a, "Add", compile_oneliner },
  { 0x0b, "Subtract", compile_oneliner },
  { 0x0c, "Multiply", compile_oneliner },
  { 0x0d, "Divide", compile_oneliner },
  { 0x0e, "Equals", compile_equals },
  { 0x0f, "Less", NULL },
  { 0x10, "And", NULL },
  { 0x11, "Or", NULL },
  { 0x12, "Not", compile_oneliner },
  { 0x13, "StringEquals", NULL },
  { 0x14, "StringLength", NULL },
  { 0x15, "StringExtract", NULL },
  { 0x17, "Pop", compile_oneliner },
  { 0x18, "ToInteger", compile_to_integer },
  { 0x1c, "GetVariable", compile_get_variable },
  { 0x1d, "SetVariable", compile_set_variable },
  { 0x20, "SetTarget2", compile_set_target_2 },
  { 0x21, "StringAdd", compile_string_add },
  { 0x22, "GetProperty", compile_get_property },
  { 0x23, "SetProperty", compile_set_property },
  { 0x24, "CloneSprite", NULL },
  { 0x25, "RemoveSprite", NULL },
  { 0x26, "Trace", compile_trace },
  { 0x27, "StartDrag", compile_start_drag },
  { 0x28, "EndDrag", compile_simple_call },
  { 0x29, "StringLess", NULL },
  /* version 7 */
  { 0x2a, "Throw", NULL },
  { 0x2b, "Cast", NULL },
  { 0x2c, "Implements", NULL },
  /* version 4 */
  { 0x30, "RandomNumber", compile_random },
  { 0x31, "MBStringLength", NULL },
  { 0x32, "CharToAscii", NULL },
  { 0x33, "AsciiToChar", NULL },
  { 0x34, "GetTime", NULL },
  { 0x35, "MBStringExtract", NULL },
  { 0x36, "MBCharToAscii", NULL },
  { 0x37, "MVAsciiToChar", NULL },
  /* version 5 */
  { 0x3a, "Delete", NULL },
  { 0x3b, "Delete2", NULL },
  { 0x3c, "DefineLocal", NULL },
  { 0x3d, "CallFunction", compile_call_function },
  { 0x3e, "Return", NULL },
  { 0x3f, "Modulo", NULL },
  { 0x40, "NewObject", compile_new_object },
  { 0x41, "DefineLocal2", NULL },
  { 0x42, "InitArray", NULL },
  { 0x43, "InitObject", compile_init_object },
  { 0x44, "Typeof", NULL },
  { 0x45, "TargetPath", compile_target_path },
  { 0x46, "Enumerate", NULL },
  { 0x47, "Add2", compile_oneliner },
  { 0x48, "Less2", compile_oneliner },
  { 0x49, "Equals2", compile_oneliner },
  { 0x4a, "ToNumber", NULL },
  { 0x4b, "ToString", NULL },
  { 0x4c, "PushDuplicate", compile_oneliner },
  { 0x4d, "Swap", NULL },
  { 0x4e, "GetMember", compile_get_member },
  { 0x4f, "SetMember", compile_oneliner_pop }, /* apparently the result is ignored */
  { 0x50, "Increment", compile_increment },
  { 0x51, "Decrement", compile_decrement },
  { 0x52, "CallMethod", compile_call_method },
  { 0x53, "NewMethod", NULL },
  /* version 6 */
  { 0x54, "InstanceOf", NULL },
  { 0x55, "Enumerate2", NULL },
  /* version 5 */
  { 0x60, "BitAnd", NULL },
  { 0x61, "BitOr", NULL },
  { 0x62, "BitXor", NULL },
  { 0x63, "BitLShift", NULL },
  { 0x64, "BitRShift", NULL },
  { 0x65, "BitURShift", NULL },
  /* version 6 */
  { 0x66, "StrictEquals", NULL },
  { 0x67, "Greater", compile_oneliner },
  { 0x68, "StringGreater", NULL },
  /* version 7 */
  { 0x69, "Extends", NULL },

  /* version 3 */
  { 0x81, "GotoFrame", compile_goto_frame },
  { 0x83, "GetURL", compile_get_url },
  /* version 5 */
  { 0x87, "StoreRegister", NULL },
  { 0x88, "ConstantPool", compile_constant_pool },
  /* version 3 */
  { 0x8a, "WaitForFrame", compile_wait_for_frame },
  { 0x8b, "SetTarget", compile_set_target },
  { 0x8c, "GotoLabel", compile_goto_label },
  /* version 4 */
  { 0x8d, "WaitForFrame2", NULL },
  /* version 7 */
  { 0x8e, "DefineFunction2", NULL },
  { 0x8f, "Try", NULL },
  /* version 5 */
  { 0x94, "With", NULL },
  /* version 4 */
  { 0x96, "Push", compile_push },
  { 0x99, "Jump", compile_jump },
  { 0x9a, "GetURL2", NULL },
  /* version 5 */
  { 0x9b, "DefineFunction", NULL },
  /* version 4 */
  { 0x9d, "If", compile_if },
  { 0x9e, "Call", NULL },
  { 0x9f, "GotoFrame2", compile_goto_frame2 }
};

int
uint_compare (gconstpointer v1, gconstpointer v2)
{
  return *((const unsigned int*) v1) - *((const unsigned int*) v2);
}

static const SwfdecActionSpec *
swfdec_action_find (guint action)
{
  return bsearch (&action, actions, G_N_ELEMENTS (actions), 
      sizeof (SwfdecActionSpec), uint_compare);
}
