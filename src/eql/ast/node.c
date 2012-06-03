#include <stdlib.h>
#include "node.h"

//==============================================================================
//
// Functions
//
//==============================================================================

//--------------------------------------
// Node Lifecycle
//--------------------------------------

// Recursively frees an AST node.
//
// node - The node to free.
void eql_ast_node_free(eql_ast_node *node)
{
    if(!node) return;
    
    // Recursively free dependent data.
    switch(node->type) {
        case EQL_AST_TYPE_INT_LITERAL: break;
        case EQL_AST_TYPE_FLOAT_LITERAL: break;
        case EQL_AST_TYPE_BINARY_EXPR: {
            eql_ast_binary_expr_free(node);
            break;
        }
        case EQL_AST_TYPE_VAR_REF: {
            eql_ast_var_ref_free(node);
            break;
        }
        case EQL_AST_TYPE_VAR_DECL: {
            eql_ast_var_decl_free(node);
            break;
        }
        case EQL_AST_TYPE_FARG: {
            eql_ast_farg_free(node);
            break;
        }
        case EQL_AST_TYPE_FUNCTION: {
            eql_ast_function_free(node);
            break;
        }
        case EQL_AST_TYPE_FCALL: {
            eql_ast_fcall_free(node);
            break;
        }
        case EQL_AST_TYPE_BLOCK: {
            eql_ast_block_free(node);
            break;
        }
        case EQL_AST_TYPE_METHOD: {
            eql_ast_method_free(node);
            break;
        }
        case EQL_AST_TYPE_PROPERTY: {
            eql_ast_property_free(node);
            break;
        }
        case EQL_AST_TYPE_CLASS: {
            eql_ast_class_free(node);
            break;
        }
        case EQL_AST_TYPE_MODULE: {
            eql_ast_module_free(node);
            break;
        }
        case EQL_AST_TYPE_METADATA: {
            eql_ast_metadata_free(node);
            break;
        }
        case EQL_AST_TYPE_METADATA_ITEM: {
            eql_ast_metadata_item_free(node);
            break;
        }
    }
    
    free(node);
}
