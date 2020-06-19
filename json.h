#ifndef __JSON_h__
#define __JSON_h__ 1

typedef struct JSON_Struct
{
	json_node_t *root;
	int nr_nodes;
} json_t;

#endif /* !defined __JSON_h__ */
