#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

/*
 * At first, I made each node have an array of child nodes.
 * Everytime we parse an item in the json object, we would
 * create a new child node for the parent.
 *
 * However, instead I have opted for each node having an
 * array of json value types. The value type holds a union
 * which allows us to have values of whichever type we like.
 * When a key-value pair within a node has as its value
 * another json object, the value will then be another json
 * node, whose key-value pairs we save in its value array.
 *
 * The json nodes have a json_value_t ** array (pointer to
 * pointer. We create new values on the heap and then save
 * their pointers in this array.
 *
 * JSON values that are arrays are json_value_t *. The last
 * entry is always a sentinel so we know where the end of
 * the array is (its value type is an int with value
 * 0xdeadbeef). Its name is also %NULL, but having the
 * integer sentinel value let's us be more sure.
 */

static void
Debug(char *fmt, ...)
{
#ifdef DEBUG
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
#else
	(void)fmt;
#endif

	return;
}

enum
{
	FALSE = 0,
	TRUE = 1
};

enum
{
	VALUE_STRING = 0,
	VALUE_NULL,
	VALUE_BOOLEAN,
	VALUE_NUMBER,
	VALUE_FLOAT,
	VALUE_DOUBLE,
	VALUE_ARRAY,
	VALUE_OBJECT
};

typedef union JSON_UValue json_uvalue_t;
typedef struct JSON_Value json_value_t;

typedef struct JSON_Node
{
	char *name;
	json_value_t **values;
	int nr_values;
} json_node_t;

union JSON_UValue
{
	char *u_string;
	json_uvalue_t *u_array;
	int u_int;
	int u_boolean;
	float u_float;
	double u_double;
	json_node_t *u_object;
};

struct JSON_Value
{
	int type;
	char *name;
	json_uvalue_t *value;
};

/*
 * We will be using this for a pointer to json_value_t, which
 * contains our json_uvalue_t *. We need to dereference the
 * json_uvalue_t * and cast it to the relevant type.
 *
 * To allow it to dereference the json_uvalue_t * within the
 * json_value_t struct, we should cast its type to a T *:
 *
 * e.g.,	VALUE_CAST(value, json_node_t *) => ((json_node_t *)(*((json_value_t **)value->value)))
 *		VALUE_CAST(value, int) => ((int)(*((int *)value->value)))
 *		VALUE_CAST(value, char **) ((char **)(*((char ***)value->value)))
 */
#define VALUE_CAST(v,t) ((t)(*((t*)v->value)))

#define CR	'\r'
#define NL	'\n'
#define TAB	'\t'

#define LBRACE	'{'
#define RBRACE	'}'
#define LBRACK	'['
#define RBRACK	']'
#define DQUOTE	'\"'
#define COMMA	','
#define SPACE	' '

#define IS_CRNL(p) ((p) == CR || (p) == NL)

enum
{
	TOK_SPACE = 0,
	TOK_LBRACE,
	TOK_RBRACE,
	TOK_LBRACK,
	TOK_RBRACK,
	TOK_DQUOTE,
	TOK_COMMA,
	TOK_CHARSEQ,
	TOK_DIGIT
};

static int
lex(void)
{
#define IS_CNTRLSPACE(p) (IS_CRNL(p) || (p) == TAB)
	while (IS_CNTRLSPACE(*ptr))
		++ptr;

	if (*ptr == SPACE && IS_CRNL(*(ptr-1)))
	{
		while (*ptr == SPACE)
			++ptr;
	}

	switch(*ptr)
	{
		case DQUOTE:
			++ptr;
			return TOK_DQUOTE;

		case LBRACE:
			++ptr;
			return TOK_LBRACE;

		case RBRACE:
			++ptr;
			return TOK_RBRACE;

		case LBRACK:
			++ptr;
			return TOK_LBRACK;

		case RBRACK:
			++ptr;
			return TOK_RBRACK;

		case SPACE:
			++ptr;
			return TOK_SPACE;

		default:
			if (isdigit(*ptr))
				return TOK_DIGIT;

			return TOK_CHARSEQ;
	}
}

static int current = -1;
static int lookahead = -1;

static void
advance(void)
{
	current = lookahead;
	lookahead = lex();
}

static int
matches(int tok)
{
	return lookahead == tok;
}

static char *ptr = NULL;

static char charseq[8192];
static void
parse_charseq(void)
{
	char *s = ptr;

repeat:
	ptr = memchr(ptr, DQUOTE, end - ptr);

	if (*(ptr-1) == '\\')
	{
		++ptr;
		goto repeat;
	}

	strncpy(charseq, s, ptr - s);
	charseq[ptr - s] = 0;

	advance();
	assert(matches(TOK_DQUOTE));

	return;
}

char *
parse_string(void)
{
	parse_charseq();
	return strdup(charseq);
}

char *
parse_string_nodquotes(void)
{
	char *s = ptr;

	while (*ptr != COMMA && *ptr != SPACE && !IS_CRNL(*ptr))
		++ptr;

	strncpy(charseq, s, ptr - s);
	charseq[ptr - s] = 0;

	return strdup(charseq);
}

char *
parse_value_name(void)
{
	char *s = parse_string();

	advance();
	assert(matches(TOK_COLON));

	return s;
}

#define NUM_CHARS_MAX 32
static char _number[NUM_CHARS_MAX];
int
parse_number(void)
{
	char *s = ptr;

	while (isdigit(*ptr))
		++ptr;

	strncpy(_number, s, ptr - s);
	_number[ptr - s] = 0;

	return atoi(_number);
}

int
string_type(char *str)
{
	assert(str);

	if (!strcmp("true", str) || !strcmp("false", str))
		return VALUE_BOOLEAN;

	return VALUE_NULL;
}

static int ARRAY_END_SENTINEL = 0xdeadbeef;

json_value_t *
parse_array(void)
{
	json_value_t *arr = NULL;
	json_value_t *o = NULL;
	int nr;
	char _s[1024];

	for (arr = calloc(1, sizeof(json_uvalue_t)), nr = 0;
		;
		arr = realloc(arr, (nr+1) * sizeof(json_uvalue_t)), ++nr) 
	{
		advance();

		o = &arr[nr];
		sprintf(_s, "#%d", nr);
		o->name = strdup(_s); 

		if (matches(TOK_DQUOTE))
		{
			VALUE_CAST(o, char *) = parse_string();
			o->type = VALUE_STRING;
		}
		else
		if (matches(TOK_MINUS))
		{
			int val = parse_number();
			VALUE_CAST(o, int) = val * -1;
			o->type = VALUE_NUMBER;
		}
		else
		if (matches(TOK_DIGIT))
		{
			// ((int)(*((int *)o->value)))
			VALUE_CAST(o, int) = parse_number();
			o->type = VALUE_NUMBER;
		}
		else
		if (matches(TOK_CHARSEQ))
		{
			char *s = parse_string();

			// ((char *)(*((char **)o->value)))
			VALUE_CAST(o, char *) = s;
			o->type = string_type(s);
		}
		else
		if (matches(TOK_ARRAY))
		{
			fprintf(stderr, "No nested arrays supported at the moment\n");
			abort();
		}

	/*
	 * Either we will parse a comma or finally the ']' character.
	 */
		advance();

		if (matches(TOK_RBRACK))
			break;
	}

/*
 * Nowhere to encode the size of the array, so copy a known
 * json_uvalue_t sentinel that resides in static memory
 * into the end of the array on the heap.
 */
	arr = realloc(arr, (nr+1) * sizeof(json_uvalue_t));
	assert(arr);

	o = &arr[nr];
	VALUE_CAST(o, int) = ARRAY_END_SENTINEL; // 0xdeadbeef
	o->name = NULL;

	return arr;
}

/**
 * Create a new JSON node. The only times we need to create
 * a node is for the root node and then for when a json value
 * is itself another json object, e.g.,:
 *
 * "item" : { ... }
 */
static json_node_t *
new_node(void)
{
	json_node_t *n = malloc(sizeof(json_node_t));

	if (NULL == n)
		return NULL;

	memset(n, 0, sizeof(*n));

	return n;
}

static json_value_t *
new_value(void)
{
	json_value_t *v = malloc(sizeof(json_value_t));

	if (NULL == v)
		return NULL;

	memset(v, 0, sizeof(*v));

	return v;
}

#define NVALS(n) ((n)->nr_values)
#define VALUES(n) ((n)->values)
#define VALUE(n,i) ((n)->values[(i)])
#define LAST_VALUE(n) VALUE(n, NVALS(n)-1)
#define FIRST_VALUE(n) VALUE(n,0)

/**
 * Add a new pointer to json_value_t to the array of value pointers
 * in the parent node (holds a json_value_t ** array).
 */
static void
add_value(json_node_t *parent, json_value_t *value)
{
	assert(parent);
	assert(value);

	if (NULL == VALUES(parent))
		VALUES(parent) = calloc(1, sizeof(json_value_t *));
	else
		VALUES(parent) = realloc(VALUES(parent), (NVALS(parent)+1) * sizeof(json_value_t *));

	assert(VALUES(parent));

	VALUE(parent, NVALS(parent)) = value;
	++NVALS(parent);

	return;
}

static json_t *
JSON_new(void)
{
	json_t *jn = malloc(sizeof(json_t));

	if (NULL == jn)
		return NULL;

	memset(jn, 0, sizeof(*jn));

	return jn;
}

/**
 * The value array is allocated as one chunk of memory on the heap.
 * So only one free is needed unlike the array of values that are
 * within JSON nodes (json_value_t **).
 */
static void
do_free_value_array(json_value_t *arr)
{
	assert(arr);

	int i;

	for (i = 0; VALUE_CAST(&arr[i], int) != 0xdeadbeef; ++i)
	{
	/*
	 * The only value types that need freed are null, boolean and string.
	 */
		switch(arr[i].type)
		{
			case VALUE_NULL:
			case VALUE_BOOLEAN:
			case VALUE_STRING:

				free(VALUE_CAST(&arr[i], char *));
				break;

			default:
				break;
		}
	}

	free(arr);

	return;
}

/**
 * Free all heap-allocated data within the json node
 * structure. All json nodes have a NAME allocated
 * on the heap. They all have a pointer to pointer
 * json_value_t ** array, requiring a free per
 * json_value_t * in the array and a free of the
 * heap memory used to hold the array of pointers.
 *
 * JSON array values are just json_value_t *, so
 * only one free is needed for that one contiguous
 * chunk of memory.
 */
static void
do_free(json_node_t *root)
{
	assert(root);

	int nr = root->nr_values;
	json_value_t *v = NULL;
	int i;

	for (i = 0; i < nr; ++i)
	{
		if (NULL != root->name)
			free(root->name);

		if (NULL == root->values)
			continue;
		// nodes hold a json_value_t ** (pointer to pointer)
		v = root->values[i];

		switch(v->type)
		{
			case VALUE_OBJECT:

				do_free(VALUE_CAST(v, json_node_t *));
				break;

		/*
		 * null and boolean types are strings ("null", "true", "false")
		 * which live on the heap and need to be freed.
		 */
			case VALUE_NULL:
			case VALUE_BOOLEAN:
			case VALUE_STRING:

				free(VALUE_CAST(v, char *));
				break;

			case VALUE_ARRAY:

				do_free_value_array(VALUE_CAST(v, json_value_t *));
				break;

			default:
				break;
		}

		if (NULL != v->name)
			free(v->name);

		free(v); // free json_value_t *
	}

	free(root->values); // free the json_value_t **
}

void
JSON_free(json_t *json)
{
	assert(json);
	if (NULL == json->root)
		return;

	do_free(json->root);

	return;
}

#define STACK_MAX_SIZE 256
// keep track of the current parent node of newly created nodes
static json_node_t *stack[STACK_MAX_SIZE];
static int stack_idx = 0;

#define CLEAR_STACK() memset(stack, 0, STACK_MAX_SIZE * sizeof(json_node_t))

#define P_PUSH(p) \
do { \
	assert(stack_idx < STACK_MAX_SIZE); \
	stack[stack_idx++] = (p); \
} while (0)

#define P_POP() \
({ \
	assert(stack_idx > 0); \
	stack[--stack_idx]; \
})

static json_node_t *node = NULL;
static json_node_t *parent = NULL;
static json_value_t *value = NULL;

#define TOGGLE_STATE() (state = (state + 1) & 1)
#define GETTING_VALUE() (state == 1)
static int state = 0;

json_t *
JSON_parse(char *json_data)
{
	assert(json_data);
	ptr = json_data;

	json_t *jn = JSON_new();
	jn->root = new_node();

	assert(jn->root);

	jn->root->name = strdup("root");
	parent = jn->root;

	CLEAR_STACK();

	advance();
	assert(matches(TOK_LBRACE));

	advance();
	int minus = 0;

	while (ptr < end)
	{
		switch(lookahead)
		{
			case TOK_MINUS:

				assert(GETTING_VALUE());

				minus = 1;
				advance();
				assert(isdigit(*ptr));

			case TOK_DIGIT:

				assert(GETTING_VALUE());

				int v = parse_number();
				if (minus)
					v *= -1;

				VALUE_CAST(value, int) = v;
				value->type = VALUE_NUMBER;

				add_value(parent, value);
				minus = 0;

				Debug("Got value %d\n", VALUE_CAST(value, int));

				TOGGLE_STATE();

				break;

			case TOK_COMMA:

				break;

			case TOK_DQUOTE:

				if (GETTING_VALUE())
				{
					VALUE_CAST(value, char *) = parse_string();
					value->type = VALUE_STRING;

					Debug("Got value %s\n", VALUE_CAST(value, char *));

					add_value(parent, value);
				}
				else
				{
					value = new_value();
					value->name = parse_value_name();

					Debug("Got name %s\n", VALUE_CAST(value, char *));
				}

				TOGGLE_STATE();

				break;

			case TOK_LBRACE:

				node = new_node();
				assert(node);

				VALUE_CAST(value, json_node_t *) = node;
				value->type = VALUE_OBJECT;

				add_value(parent, value);
			/*
			 * The values we will be parsing after this will belong
			 * to the newly-created node. So push the parent onto
			 * the stack and then the new node becomes the parent.
			 */
				P_PUSH(parent);
				parent = node;

				break;

			case TOK_RBRACE:

				parent = P_POP();
				break;

			case TOK_LBRACK:

				Debug("Parsing JSON array\n");
				value->type = VALUE_ARRAY;

				// ((json_value_t *)(*((json_value_t **)value->value)))
				VALUE_CAST(value, json_value_t *) = parse_array();

				add_value(parent, value);

				break;

			case TOK_CHARSEQ:
		/*
		 * If we are about to parse a value and we didn't find a DQUOTE,
		 * then it must be null, or true/false.
		 */
				VALUE_CAST(value, char *) = parse_string_nodquotes();
				value->type = string_type(VALUE_CAST(value, char *));

			default:
				break;
		}

		advance();
	}

	return json;
}

int
main(void)
{
	char json_data[] =
		"{\n"
		"\t\"item1\" : \"value1\"\n"
		"\t\"item2\" : \"value2\"\n"
		"\t\"item3\" : {\n"
		"\t\t\"sub1\" : \"subvalue1\"\n"
		"\t\t\"sub2\" : \"subvalue2\"\n"
		"\t}\n"
		"\t\"item4\" : [ \"first\", \"second\", \"third\", \"fourth\" ]\n"
		"}";

	json_t *JSON_Obj = JSON_parse(json_data);
	assert(JSON_Obj);

	JSON_free(JSON_Obj);

	return 0;
}
