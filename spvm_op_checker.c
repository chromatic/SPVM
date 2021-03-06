#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>


#include "spvm_compiler.h"
#include "spvm_array.h"
#include "spvm_hash.h"
#include "spvm_compiler_allocator.h"
#include "spvm_yacc_util.h"
#include "spvm_op.h"
#include "spvm_sub.h"
#include "spvm_constant.h"
#include "spvm_field.h"
#include "spvm_my_var.h"
#include "spvm_var.h"
#include "spvm_enumeration_value.h"
#include "spvm_type.h"
#include "spvm_enumeration.h"
#include "spvm_package.h"
#include "spvm_name_info.h"
#include "spvm_type.h"
#include "spvm_switch_info.h"
#include "spvm_constant_pool.h"
#include "spvm_limit.h"

void SPVM_OP_CHECKER_build_leave_scope(SPVM_COMPILER* compiler, SPVM_OP* op_leave_scope, SPVM_ARRAY* op_my_var_stack, int32_t top, int32_t bottom, SPVM_OP* op_term_keep) {
  
  for (int32_t i = top; i >= bottom; i--) {
    SPVM_OP* op_my_var = SPVM_ARRAY_fetch(op_my_var_stack, i);
    assert(op_my_var);
    
    SPVM_TYPE* type = SPVM_OP_get_type(compiler, op_my_var);
    
    // Decrement reference count when leaving scope
    if (!SPVM_TYPE_is_numeric(compiler, type)) {
      // If return term is variable, don't decrement reference count
      _Bool do_dec_ref_count = 0;
      if (op_term_keep) {
        if (op_term_keep->code == SPVM_OP_C_CODE_VAR) {
          if (op_term_keep->uv.var->op_my_var->uv.my_var->index != op_my_var->uv.my_var->index) {
            do_dec_ref_count = 1;
          }
        }
        else {
          do_dec_ref_count = 1;
        }
      }
      else {
        do_dec_ref_count = 1;
      }
      
      if (do_dec_ref_count) {
        SPVM_OP* op_dec_ref_count = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_DEC_REF_COUNT, op_leave_scope->file, op_leave_scope->line);
        SPVM_OP* op_var = SPVM_OP_new_op_var_from_op_my_var(compiler, op_my_var);
        SPVM_OP_sibling_splice(compiler, op_dec_ref_count, NULL, 0, op_var);
        SPVM_OP_sibling_splice(compiler, op_leave_scope, NULL, 0, op_dec_ref_count);
      }
    }
  }
}

void SPVM_OP_CHECKER_check(SPVM_COMPILER* compiler) {
  
  SPVM_ARRAY* op_types = compiler->op_types;
  
  // Types
  for (int32_t i = 0, len = op_types->length; i < len; i++) {
    assert(compiler->types->length <= SPVM_LIMIT_C_TYPES);
    
    SPVM_OP* op_type = SPVM_ARRAY_fetch(op_types, i);
    
    if (compiler->types->length == SPVM_LIMIT_C_TYPES) {
      SPVM_yyerror_format(compiler, "too many types at %s line %d\n", op_type->file, op_type->line);
      compiler->fatal_error = 1;
      return;
    }
    
    _Bool success = SPVM_TYPE_resolve_type(compiler, op_type, 0);
    
    if (!success) {
      compiler->fatal_error = 1;
      return;
    }
  }

  // Reorder fields. Reference types place before value types.
  SPVM_ARRAY* op_packages = compiler->op_packages;
  for (int32_t package_pos = 0; package_pos < op_packages->length; package_pos++) {
    SPVM_OP* op_package = SPVM_ARRAY_fetch(op_packages, package_pos);
    SPVM_PACKAGE* package = op_package->uv.package;
    SPVM_ARRAY* op_fields = package->op_fields;
    
    SPVM_ARRAY* op_fields_ref = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
    SPVM_ARRAY* op_fields_value = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);

    // Separate reference type and value type
    _Bool field_type_error = 0;
    for (int32_t field_pos = 0; field_pos < op_fields->length; field_pos++) {
      SPVM_OP* op_field = SPVM_ARRAY_fetch(op_fields, field_pos);
      SPVM_FIELD* field = op_field->uv.field;
      SPVM_TYPE* field_type = field->op_type->uv.type;
      
      // Check field type
      if (SPVM_TYPE_is_array(compiler, field_type)) {
        if (!SPVM_TYPE_is_array_numeric(compiler, field_type)) {
          SPVM_yyerror_format(compiler, "Type of field \"%s::%s\" must not be object array at %s line %d\n", package->op_name->uv.name, field->op_name->uv.name, op_field->file, op_field->line);
          field_type_error = 1;
        }
      }
      else if (!SPVM_TYPE_is_numeric(compiler, field_type)) {
          SPVM_yyerror_format(compiler, "Type of field \"%s::%s\" must not be object at %s line %d\n", package->op_name->uv.name, field->op_name->uv.name, op_field->file, op_field->line);
        field_type_error = 1;
      }
    }
    if (field_type_error) {
      compiler->fatal_error = 1;
      return;
    }
    
    // Separate reference type and value type
    int32_t ref_fields_length = 0;
    for (int32_t field_pos = 0; field_pos < op_fields->length; field_pos++) {
      SPVM_OP* op_field = SPVM_ARRAY_fetch(op_fields, field_pos);
      SPVM_FIELD* field = op_field->uv.field;
      SPVM_TYPE* field_type = field->op_type->uv.type;
      
      // Check field type
      if (SPVM_TYPE_is_array(compiler, field_type)) {
        if (!SPVM_TYPE_is_array_numeric(compiler, field_type)) {
          SPVM_yyerror_format(compiler, "field type must be numeric or numeric array or string array at %s line %d\n", op_field->file, op_field->line);
          compiler->fatal_error = 1;
          return;
        }
      }
      else if (!SPVM_TYPE_is_numeric(compiler, field_type)) {
        SPVM_yyerror_format(compiler, "field type must be numeric or numeric array or string array at %s line %d\n", op_field->file, op_field->line);
        compiler->fatal_error = 1;
        return;
      }
      
      if (SPVM_TYPE_is_numeric(compiler, field_type)) {
        SPVM_ARRAY_push(op_fields_value, op_field);
      }
      else {
        SPVM_ARRAY_push(op_fields_ref, op_field);
        ref_fields_length++;
      }
    }
    package->ref_fields_length = ref_fields_length;
    
    // Create ordered op fields
    SPVM_ARRAY* ordered_op_fields = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
    for (int32_t field_pos = 0; field_pos < op_fields_ref->length; field_pos++) {
      SPVM_OP* op_field = SPVM_ARRAY_fetch(op_fields_ref, field_pos);
      SPVM_ARRAY_push(ordered_op_fields, op_field);
    }
    for (int32_t field_pos = 0; field_pos < op_fields_value->length; field_pos++) {
      SPVM_OP* op_field = SPVM_ARRAY_fetch(op_fields_value, field_pos);
      SPVM_ARRAY_push(ordered_op_fields, op_field);
    }
    package->op_fields = ordered_op_fields;
  }
  
  // Resolve package
  for (int32_t package_pos = 0; package_pos < op_packages->length; package_pos++) {
    SPVM_OP* op_package = SPVM_ARRAY_fetch(op_packages, package_pos);
    SPVM_PACKAGE* package = op_package->uv.package;
    SPVM_ARRAY* op_fields = package->op_fields;
    
    // Calculate package byte size
    for (int32_t field_pos = 0; field_pos < op_fields->length; field_pos++) {
      SPVM_OP* op_field = SPVM_ARRAY_fetch(op_fields, field_pos);
      SPVM_FIELD* field = op_field->uv.field;
      field->index = field_pos;
    }
    package->fields_length = op_fields->length;
  }
  
  for (int32_t package_pos = 0; package_pos < op_packages->length; package_pos++) {
    SPVM_OP* op_package = SPVM_ARRAY_fetch(op_packages, package_pos);
    SPVM_PACKAGE* package = op_package->uv.package;
    
    if (strchr(package->op_name->uv.name, '_') != NULL) {
      SPVM_yyerror_format(compiler, "Package name can't contain _ at %s line %d\n", op_package->file, op_package->line);
      compiler->fatal_error = 1;
      return;
    }
    
    // Constant pool
    SPVM_CONSTANT_POOL* constant_pool = compiler->constant_pool;
    
    // Push field information to constant pool
    for (int32_t field_pos = 0; field_pos < package->op_fields->length; field_pos++) {
      SPVM_OP* op_field = SPVM_ARRAY_fetch(package->op_fields, field_pos);
      SPVM_FIELD* field = op_field->uv.field;
      
      // Add field abs name to constant pool
      field->abs_name_constant_pool_index = compiler->constant_pool->length;
      SPVM_CONSTANT_POOL_push_string(compiler, compiler->constant_pool, field->abs_name);

      // Add field name to constant pool
      field->name_constant_pool_index = compiler->constant_pool->length;
      SPVM_CONSTANT_POOL_push_string(compiler, compiler->constant_pool, field->op_name->uv.name);
      
      // Add field to constant pool
      field->constant_pool_index = compiler->constant_pool->length;
      SPVM_CONSTANT_POOL_push_field(compiler, compiler->constant_pool, field);
    }
    
    // Push fields name indexes to constant pool
    package->field_name_indexes_constant_pool_index = constant_pool->length;
    SPVM_CONSTANT_POOL_push_int(compiler, constant_pool, package->op_fields->length);
    for (int32_t field_pos = 0; field_pos < package->op_fields->length; field_pos++) {
      SPVM_OP* op_field = SPVM_ARRAY_fetch(package->op_fields, field_pos);
      SPVM_FIELD* field = op_field->uv.field;
      SPVM_CONSTANT_POOL_push_int(compiler, constant_pool, field->name_constant_pool_index);
    }
    
    // Push package name to constant pool
    const char* package_name = package->op_name->uv.name;
    package->name_constant_pool_index = constant_pool->length;
    SPVM_CONSTANT_POOL_push_string(compiler, constant_pool, package_name);
    
    // Push package information to constant pool
    package->constant_pool_index = constant_pool->length;
    SPVM_CONSTANT_POOL_push_package(compiler, constant_pool, package);
    
    for (int32_t sub_pos = 0; sub_pos < package->op_subs->length; sub_pos++) {
      
      SPVM_OP* op_sub = SPVM_ARRAY_fetch(package->op_subs, sub_pos);
      SPVM_SUB* sub = op_sub->uv.sub;
      
      // Only process normal subroutine
      if (!sub->is_constant && !sub->is_native) {
        
        // my var informations
        SPVM_ARRAY* op_my_vars = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
        
        // my variable stack
        SPVM_ARRAY* op_my_var_stack = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
        
        // block my variable base position stack
        SPVM_ARRAY* block_my_var_base_stack = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
        int32_t block_my_var_base = 0;

        // try block my variable base position stack
        SPVM_ARRAY* try_block_my_var_base_stack = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
        
        // loop block my variable base position stack
        SPVM_ARRAY* loop_block_my_var_base_stack = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
        
        // In switch statement
        _Bool in_switch = 0;
        
        // Current case statements
        SPVM_ARRAY* cur_case_ops = NULL;
        
        // Current default statement
        SPVM_OP* cur_default_op = NULL;
        
        // op count
        int32_t op_count = 0;
        
        int32_t my_var_length = 0;
        
        // Run OPs
        SPVM_OP* op_base = SPVM_OP_get_op_block_from_op_sub(compiler, op_sub);
        SPVM_OP* op_cur = op_base;
        _Bool finish = 0;
        while (op_cur) {
          
          op_count++;
          
          // [START]Preorder traversal position
          
          switch (op_cur->code) {
            case SPVM_OP_C_CODE_AND: {
              
              // Convert && to if statement
              SPVM_OP_convert_and_to_if(compiler, op_cur);
              
              break;
            }
            case SPVM_OP_C_CODE_OR: {
              
              // Convert || to if statement
              SPVM_OP_convert_or_to_if(compiler, op_cur);
              
              break;
            }
            case SPVM_OP_C_CODE_NOT: {
              // Convert ! to if statement
              SPVM_OP_convert_not_to_if(compiler, op_cur);
              
              break;
            }
            case SPVM_OP_C_CODE_SWITCH: {
              if (in_switch) {
                SPVM_yyerror_format(compiler, "duplicate switch is forbidden at %s line %d\n", op_cur->file, op_cur->line);
                compiler->fatal_error = 1;
                return;
              }
              else {
                in_switch = 1;
              }
              
              break;
            }
            // Start scope
            case SPVM_OP_C_CODE_BLOCK: {
              
              // Add return to the end of subroutine
              if (op_cur->flag & SPVM_OP_C_FLAG_BLOCK_SUB) {
                SPVM_OP* op_statements = op_cur->first;
                
                if (op_statements->last->code != SPVM_OP_C_CODE_RETURN_PROCESS) {
                  
                  SPVM_OP* op_return = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_RETURN, op_cur->file, op_cur->line);
                  if (sub->op_return_type->code != SPVM_OP_C_CODE_VOID) {
                    SPVM_TYPE* op_return_type = SPVM_OP_get_type(compiler, sub->op_return_type);
                    if (op_return_type) {
                      if (SPVM_TYPE_is_numeric(compiler, op_return_type)) {
                        SPVM_OP* op_constant;
                        if (op_return_type->id <= SPVM_TYPE_C_ID_INT) {
                          op_constant = SPVM_OP_new_op_constant_int(compiler, 0, op_cur->file, op_cur->line);
                        }
                        else if (op_return_type->id == SPVM_TYPE_C_ID_LONG) {
                          op_constant = SPVM_OP_new_op_constant_long(compiler, 0, op_cur->file, op_cur->line);
                        }
                        else if (op_return_type->id == SPVM_TYPE_C_ID_FLOAT) {
                          op_constant = SPVM_OP_new_op_constant_float(compiler, 0, op_cur->file, op_cur->line);
                        }
                        else if (op_return_type->id == SPVM_TYPE_C_ID_DOUBLE) {
                          op_constant = SPVM_OP_new_op_constant_double(compiler, 0, op_cur->file, op_cur->line);
                        }
                        else {
                          assert(0);
                        }
                        
                        SPVM_OP_sibling_splice(compiler, op_return, NULL, 0, op_constant);
                      }
                      // Reference
                      else {
                        // Undef
                        SPVM_OP* op_undef = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_UNDEF, op_cur->file, op_cur->line);
                        SPVM_OP_sibling_splice(compiler, op_return, NULL, 0, op_undef);
                      }
                    }
                  }
                  
                  SPVM_OP* op_return_process = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_RETURN_PROCESS, op_return->file, op_return->line);
                  SPVM_OP* op_leave_scope = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_LEAVE_SCOPE, op_return->file, op_return->line);
                  
                  SPVM_OP_sibling_splice(compiler, op_return_process, op_return_process->last, 0, op_leave_scope);
                  SPVM_OP_sibling_splice(compiler, op_return_process, op_return_process->last, 0, op_return);
                  
                  SPVM_OP_sibling_splice(compiler, op_statements, op_statements->last, 0, op_return_process);
                }
              }
              
              block_my_var_base = op_my_var_stack->length;
              int32_t* block_my_var_base_ptr = SPVM_COMPILER_ALLOCATOR_alloc_int(compiler, compiler->allocator);
              *block_my_var_base_ptr = block_my_var_base;
              SPVM_ARRAY_push(block_my_var_base_stack, block_my_var_base_ptr);
              
              if (op_cur->flag & SPVM_OP_C_FLAG_BLOCK_LOOP) {
                SPVM_ARRAY_push(loop_block_my_var_base_stack, block_my_var_base_ptr);
              }
              else if (op_cur->flag & SPVM_OP_C_FLAG_BLOCK_TRY) {
                SPVM_ARRAY_push(try_block_my_var_base_stack, block_my_var_base_ptr);
              }
              
              break;
            }
            case SPVM_OP_C_CODE_ASSIGN: {
              op_cur->first->lvalue = 1;
              break;
            }
          }
          
          // [END]Preorder traversal position
          if (op_cur->first) {
            op_cur = op_cur->first;
          }
          else {
            while (1) {
              // [START]Postorder traversal position
              switch (op_cur->code) {
                case SPVM_OP_C_CODE_NEXT: {
                  if (loop_block_my_var_base_stack->length == 0) {
                    SPVM_yyerror_format(compiler, "next statement must be in loop block at %s line %d\n", op_cur->file, op_cur->line);
                  }
                  break;
                }
                case SPVM_OP_C_CODE_LAST: {
                  if (loop_block_my_var_base_stack->length == 0) {
                    SPVM_yyerror_format(compiler, "last statement must be in loop block at %s line %d\n", op_cur->file, op_cur->line);
                  }
                  break;
                }
                case SPVM_OP_C_CODE_NEXT_PROCESS: {
                  SPVM_OP* op_leave_scope = op_cur->first;
                  
                  assert(loop_block_my_var_base_stack->length > 0);
                  int32_t* bottom_ptr = SPVM_ARRAY_fetch(loop_block_my_var_base_stack, loop_block_my_var_base_stack->length - 1);
                  
                  // Build op_leave_scope
                  SPVM_OP_CHECKER_build_leave_scope(compiler, op_leave_scope, op_my_var_stack, op_my_var_stack->length - 1, *bottom_ptr, NULL);
                  
                  break;
                }
                case SPVM_OP_C_CODE_LAST_PROCESS: {
                  SPVM_OP* op_leave_scope = op_cur->first;
                  
                  assert(loop_block_my_var_base_stack->length > 0);
                  int32_t* bottom_ptr = SPVM_ARRAY_fetch(loop_block_my_var_base_stack, loop_block_my_var_base_stack->length - 1);
                  
                  // Build op_leave_scope
                  SPVM_OP_CHECKER_build_leave_scope(compiler, op_leave_scope, op_my_var_stack, op_my_var_stack->length - 1, *bottom_ptr, NULL);
                  
                  break;
                }
                case SPVM_OP_C_CODE_DIE_PROCESS: {
                  // Add before return process
                  SPVM_OP* op_leave_scope = op_cur->first;
                  SPVM_OP* op_die = op_cur->last;
                  SPVM_OP* op_term = op_die->first;
                  
                  if (try_block_my_var_base_stack->length > 0) {
                    int32_t* bottom_ptr = SPVM_ARRAY_fetch(try_block_my_var_base_stack, try_block_my_var_base_stack->length - 1);
                    
                    // Build op_leave_scope
                    SPVM_OP_CHECKER_build_leave_scope(compiler, op_leave_scope, op_my_var_stack, op_my_var_stack->length - 1, *bottom_ptr, op_term);
                  }
                  else {
                    // Build op_leave_scope
                    SPVM_OP_CHECKER_build_leave_scope(compiler, op_leave_scope, op_my_var_stack, op_my_var_stack->length - 1, 0, op_term);
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_CONSTANT: {
                  SPVM_CONSTANT* constant = op_cur->uv.constant;
                  
                  SPVM_CONSTANT_POOL* constant_pool = compiler->constant_pool;
                  
                  constant->constant_pool_index = constant_pool->length;
                  
                  switch (constant->code) {
                    case SPVM_CONSTANT_C_CODE_INT: {
                      int64_t value = constant->uv.long_value;
                      if (value >= -32768 && value <= 32767) {
                        constant->constant_pool_index = -1;
                        break;
                      }
                      
                      SPVM_CONSTANT_POOL_push_int(compiler, constant_pool, (int32_t)value);
                      break;
                    }
                    case SPVM_CONSTANT_C_CODE_LONG: {
                      int64_t value = constant->uv.long_value;
                      
                      if (value >= -32768 && value <= 32767) {
                        constant->constant_pool_index = -1;
                        break;
                      }
                      
                      SPVM_CONSTANT_POOL_push_long(compiler, constant_pool, value);
                      break;
                    }
                    case SPVM_CONSTANT_C_CODE_FLOAT: {
                      float value = constant->uv.float_value;
                      
                      if (value == 0 || value == 1 || value == 2) {
                        constant->constant_pool_index = -1;
                        break;
                      }
                      
                      SPVM_CONSTANT_POOL_push_float(compiler, constant_pool, value);
                      break;
                    }
                    case SPVM_CONSTANT_C_CODE_DOUBLE: {
                      double value = constant->uv.double_value;
                      
                      if (value == 0 || value == 1) {
                        constant->constant_pool_index = -1;
                        break;
                      }
                      
                      SPVM_CONSTANT_POOL_push_double(compiler, constant_pool, value);
                      break;
                    }
                    case SPVM_CONSTANT_C_CODE_STRING: {
                      const char* value = constant->uv.string_value;
                      
                      SPVM_CONSTANT_POOL_push_string(compiler, constant_pool, value);
                      break;
                    }
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_POP: {
                  if (op_cur->first->code == SPVM_OP_C_CODE_CALL_SUB) {
                    SPVM_OP* op_call_sub = op_cur->first;
                    
                    const char* sub_name = op_call_sub->uv.name_info->resolved_name;
                    
                    SPVM_OP* op_sub= SPVM_HASH_search(
                      compiler->op_sub_symtable,
                      sub_name,
                      strlen(sub_name)
                    );
                    SPVM_SUB* sub = op_sub->uv.sub;
                    
                    if (sub->op_return_type->code == SPVM_OP_C_CODE_VOID) {
                      op_cur->code = SPVM_OP_C_CODE_NULL;
                    }
                  }
                  break;
                }
                case SPVM_OP_C_CODE_DEFAULT: {
                  if (cur_default_op) {
                    SPVM_yyerror_format(compiler, "multiple default is forbidden at %s line %d\n", op_cur->file, op_cur->line);
                    compiler->fatal_error = 1;
                    break;
                  }
                  else {
                    cur_default_op = op_cur;
                  }
                  break;
                }
                case SPVM_OP_C_CODE_CASE: {

                  if (!cur_case_ops) {
                    cur_case_ops = SPVM_COMPILER_ALLOCATOR_alloc_array(compiler, compiler->allocator, 0);
                  }
                  SPVM_ARRAY_push(cur_case_ops, op_cur);
                  
                  break;
                }
                case SPVM_OP_C_CODE_SWITCH: {
                  
                  SPVM_OP* op_switch_condition = op_cur->first;
                  
                  SPVM_TYPE* term_type = SPVM_OP_get_type(compiler, op_switch_condition->first);
                  
                  // Check type
                  if (!term_type || !(term_type->id == SPVM_TYPE_C_ID_INT)) {
                    SPVM_yyerror_format(compiler, "Switch condition need int value at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  in_switch = 0;
                  cur_default_op = NULL;
                  cur_case_ops = NULL;
                  
                  // tableswitch if the following. SWITCHRTIO is 1.5 by default
                  // 4 + range <= (3 + 2 * length) * SWITCHRTIO
                  
                  SPVM_SWITCH_INFO* switch_info = op_cur->uv.switch_info;
                  SPVM_ARRAY* op_cases = switch_info->op_cases;
                  int32_t length = op_cases->length;
                  
                  // Check case type
                  _Bool has_syntax_error = 0;
                  for (int32_t i = 0; i < length; i++) {
                    SPVM_OP* op_case = SPVM_ARRAY_fetch(op_cases, i);
                    SPVM_OP* op_constant = op_case->first;

                    if (op_constant->code != SPVM_OP_C_CODE_CONSTANT) {
                      SPVM_yyerror_format(compiler, "case need constant at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                    
                    SPVM_TYPE* case_value_type = SPVM_OP_get_type(compiler, op_constant);
                    
                    if (case_value_type->id != term_type->id) {
                      SPVM_yyerror_format(compiler, "case value type must be same as switch condition value type at %s line %d\n", op_case->file, op_case->line);
                      has_syntax_error = 1;
                      break;
                    }
                  }
                  if (has_syntax_error) {
                    break;
                  }
                  
                  int32_t min = INT32_MAX;
                  int32_t max = INT32_MIN;
                  for (int32_t i = 0; i < length; i++) {
                    SPVM_OP* op_case = SPVM_ARRAY_fetch(op_cases, i);
                    SPVM_OP* op_constant = op_case->first;
                    int32_t value = (int32_t)op_constant->uv.constant->uv.long_value;
                    
                    if (value < min) {
                      min = value;
                    }
                    if (value > max) {
                      max = value;
                    }
                  }
                  
                  double range = (double) max - (double) min;
                  
                  int32_t code;
                  if (4.0 + range <= (3.0 + 2.0 * (double) length) * 1.5) {
                    code = SPVM_SWITCH_INFO_C_CODE_TABLE_SWITCH;
                  }
                  else {
                    code = SPVM_SWITCH_INFO_C_CODE_LOOKUP_SWITCH;
                  }
                  
                  switch_info->code = code;
                  switch_info->min = min;
                  switch_info->max = max;
                  
                  break;
                }
                
                case SPVM_OP_C_CODE_EQ: {
                  
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // TERM == TERM
                  if (first_type && last_type) {
                    // core == core
                    if (SPVM_TYPE_is_numeric(compiler, first_type) && SPVM_TYPE_is_numeric(compiler, last_type)) {
                      if (first_type->id != last_type->id) {
                        SPVM_yyerror_format(compiler, "== operator two operands must be same type at %s line %d\n", op_cur->file, op_cur->line);
                        break;
                      }
                    }
                    // core == OBJ
                    else if (SPVM_TYPE_is_numeric(compiler, first_type)) {
                      SPVM_yyerror_format(compiler, "== left value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                    // OBJ == core
                    else if (SPVM_TYPE_is_numeric(compiler, last_type)) {
                      SPVM_yyerror_format(compiler, "== right value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  // undef == TERM
                  else if (!first_type) {
                    if (SPVM_TYPE_is_numeric(compiler, last_type)) {
                      SPVM_yyerror_format(compiler, "== right value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  // TERM == undef
                  else if (!last_type) {
                    if (SPVM_TYPE_is_numeric(compiler, first_type)) {
                      SPVM_yyerror_format(compiler, "== left value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_NE: {

                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);

                  // TERM == TERM
                  if (first_type && last_type) {
                    // core == core
                    if (SPVM_TYPE_is_numeric(compiler, first_type) && SPVM_TYPE_is_numeric(compiler, last_type)) {
                      if (first_type->id != last_type->id) {
                        SPVM_yyerror_format(compiler, "!= operator two operands must be same type at %s line %d\n", op_cur->file, op_cur->line);
                        break;
                      }
                    }
                    // core == OBJ
                    else if (SPVM_TYPE_is_numeric(compiler, first_type)) {
                      SPVM_yyerror_format(compiler, "!= left value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                    // OBJ == core
                    else if (SPVM_TYPE_is_numeric(compiler, last_type)) {
                      SPVM_yyerror_format(compiler, "!= right value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  // undef == TERM
                  else if (!first_type) {
                    if (SPVM_TYPE_is_numeric(compiler, last_type)) {
                      SPVM_yyerror_format(compiler, "!= right value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  // TERM == undef
                  else if (!last_type) {
                    if (SPVM_TYPE_is_numeric(compiler, first_type)) {
                      SPVM_yyerror_format(compiler, "!= left value must be object at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_LT: {

                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // undef check
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "< left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "< right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Can receive only core type
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "< left value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!SPVM_TYPE_is_numeric(compiler, last_type)) {
                    SPVM_yyerror_format(compiler, "< right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }

                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "< operator two operands must be same type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }

                  break;
                }
                case SPVM_OP_C_CODE_LE: {

                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);

                  // undef check
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "<= left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "<= right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                                  
                  // Can receive only core type
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "<= left value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!SPVM_TYPE_is_numeric(compiler, last_type)) {
                    SPVM_yyerror_format(compiler, "<= right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }

                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "<= operator two operands must be same type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_GT: {

                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);

                  // undef check
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "> left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "> right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Can receive only core type
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "> left value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!SPVM_TYPE_is_numeric(compiler, last_type)) {
                    SPVM_yyerror_format(compiler, "> right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }

                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "> operator two operands must be same type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_GE: {

                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);

                  // undef check
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "<= left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "<= right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Can receive only core type
                  if (SPVM_TYPE_is_numeric(compiler, first_type) && !SPVM_TYPE_is_numeric(compiler, last_type)) {
                    SPVM_yyerror_format(compiler, ">= left value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (!SPVM_TYPE_is_numeric(compiler, first_type) && SPVM_TYPE_is_numeric(compiler, last_type)) {
                    SPVM_yyerror_format(compiler, ">= right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }

                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, ">= operator two operands must be same type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_LEFT_SHIFT: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Can receive only core type
                  if (!SPVM_TYPE_is_integral(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "<< operator left value must be integral at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (last_type->id != SPVM_TYPE_C_ID_INT) {
                    SPVM_yyerror_format(compiler, "<< operator right value must be int at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_RIGHT_SHIFT: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Can receive only core type
                  if (!SPVM_TYPE_is_integral(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, ">> operator left value must be integral at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (last_type->id != SPVM_TYPE_C_ID_INT) {
                    SPVM_yyerror_format(compiler, ">> operator right value must be int at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_RIGHT_SHIFT_UNSIGNED: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Can receive only core type
                  if (!SPVM_TYPE_is_integral(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, ">>> operator left value must be integral at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  if (last_type->id > SPVM_TYPE_C_ID_INT) {
                    SPVM_yyerror_format(compiler, ">>> operator right value must be int at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_MALLOC: {
                  SPVM_OP* op_type = op_cur->first;
                  SPVM_TYPE* type = op_type->uv.type;
                  
                  if (SPVM_TYPE_is_array(compiler, type)) {
                    SPVM_OP* op_index_term = op_type->last;
                    SPVM_TYPE* index_type = SPVM_OP_get_type(compiler, op_index_term);
                    
                    if (!index_type) {
                      SPVM_yyerror_format(compiler, "new operator can't create array which don't have length \"%s\" at %s line %d\n", type->name, op_cur->file, op_cur->line);
                      break;
                    }
                    else if (index_type->id != SPVM_TYPE_C_ID_INT) {
                      SPVM_yyerror_format(compiler, "new operator can't create array which don't have int length \"%s\" at %s line %d\n", type->name, op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  else {
                    if (SPVM_TYPE_is_numeric(compiler, type)) {
                      SPVM_yyerror_format(compiler,
                        "new operator can't receive core type at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_BIT_XOR: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Can receive only core type
                  if (first_type->id >= SPVM_TYPE_C_ID_FLOAT || last_type->id >= SPVM_TYPE_C_ID_FLOAT) {
                    SPVM_yyerror_format(compiler,
                      "& operator can receive only integral type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_BIT_OR: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Can receive only core type
                  if (first_type->id >= SPVM_TYPE_C_ID_FLOAT || last_type->id >= SPVM_TYPE_C_ID_FLOAT) {
                    SPVM_yyerror_format(compiler,
                      "& operator can receive only integral type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_BIT_AND: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Can receive only core type
                  if (first_type->id >= SPVM_TYPE_C_ID_FLOAT || last_type->id >= SPVM_TYPE_C_ID_FLOAT) {
                    SPVM_yyerror_format(compiler,
                      "& operator can receive only integral type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_ARRAY_LENGTH: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  
                  // First value must be array
                  _Bool first_type_is_array = SPVM_TYPE_is_array(compiler, first_type);
                  if (!first_type_is_array) {
                    SPVM_yyerror_format(compiler, "right of @ must be array at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_ARRAY_ELEM: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // First value must be array
                  _Bool first_type_is_array = SPVM_TYPE_is_array(compiler, first_type);
                  if (!first_type_is_array) {
                    SPVM_yyerror_format(compiler, "left value must be array at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Last value must be integer
                  if (last_type->id != SPVM_TYPE_C_ID_INT) {
                    SPVM_yyerror_format(compiler, "array index must be int at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_ASSIGN: {
                  
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Type assumption
                  if (!first_type) {
                    SPVM_OP* op_var = op_cur->first;
                    SPVM_MY_VAR* my_var = op_var->uv.var->op_my_var->uv.my_var;
                    first_type = SPVM_OP_get_type(compiler, my_var->op_term_assumption);
                    
                    if (first_type) {
                      SPVM_OP* op_type = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_TYPE, op_cur->file, op_cur->line);
                      op_type->uv.type = first_type;
                      my_var->op_type = op_type;
                    }
                    else {
                      SPVM_yyerror_format(compiler, "Type can't be detected at %s line %d\n", op_cur->first->file, op_cur->first->line);
                      compiler->fatal_error = 1;
                      return;
                    }
                  }
                  
                  // It is OK that left type is object and right is undef
                  if (!SPVM_TYPE_is_numeric(compiler, first_type) && !last_type) {
                    // OK
                  }
                  // Invalid type
                  else if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "Invalid type value is assigned at %s line %d\n", op_cur->file, op_cur->line);
                    compiler->fatal_error = 1;
                    return;
                  }
                  
                  // Insert var op
                  if (op_cur->last->code == SPVM_OP_C_CODE_ASSIGN) {
                    SPVM_OP* op_var = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_VAR, op_cur->file, op_cur->line);
                    op_var->uv.var = op_cur->last->first->uv.var;
                    
                    SPVM_OP* op_last_old = op_cur->last;
                    
                    op_last_old->sibparent = op_var;
                    
                    op_var->first = op_last_old;
                    op_var->sibparent = op_cur;
                    
                    op_cur->last = op_var;
                    
                    op_cur->first->sibparent = op_var;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_RETURN: {
                  
                  SPVM_OP* op_term = op_cur->first;
                  
                  if (op_term) {
                    SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_term);
                    SPVM_TYPE* sub_return_type = SPVM_OP_get_type(compiler, sub->op_return_type);
                    
                    _Bool is_invalid = 0;
                    
                    // Undef
                    if (op_term->code == SPVM_OP_C_CODE_UNDEF) {
                      if (sub->op_return_type->code == SPVM_OP_C_CODE_VOID) {
                        is_invalid = 1;
                      }
                      else {
                        if (SPVM_TYPE_is_numeric(compiler, sub_return_type)) {
                          is_invalid = 1;
                        }
                      }
                    }
                    // Normal
                    else if (op_term) {
                      if (first_type->id != sub_return_type->id) {
                        is_invalid = 1;
                      }
                    }
                    // Empty
                    else {
                      if (sub->op_return_type->code != SPVM_OP_C_CODE_VOID) {
                        is_invalid = 1;
                      }
                    }
                    
                    if (is_invalid) {
                      SPVM_yyerror_format(compiler, "Invalid return type at %s line %d\n", op_cur->file, op_cur->line);
                      break;
                    }
                  }
                  break;
                }
                case SPVM_OP_C_CODE_RETURN_PROCESS: {
                  
                  // Add before return process
                  SPVM_OP* op_leave_scope = op_cur->first;
                  SPVM_OP* op_return = op_cur->last;
                  SPVM_OP* op_term = op_return->first;
                  
                  SPVM_OP_CHECKER_build_leave_scope(compiler, op_leave_scope, op_my_var_stack, op_my_var_stack->length - 1, 0, op_term);
                  
                  break;
                }
                case SPVM_OP_C_CODE_NEGATE: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  
                  // Must be int, long, float, double
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "Type of - operator right value must be int, long, float, double at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_PLUS: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  
                  // Must be int, long, float, double
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "Type of + operator right value must be int, long, float, double at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_ADD: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Left value must not be undef
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "+ operator left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Right value Must not be undef
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "+ operator right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Must be same type
                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "Type of + operator left and right value must be same at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                                                  
                  // Value must be int, long, float, double
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "Type of + operator left and right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_SUBTRACT: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Left value must not be undef
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "- operator left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Right value Must not be undef
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "- operator right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Must be same type
                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "Type of - operator left and right value must be same at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                                                  
                  // Value must be int, long, float, double
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "Type of - operator left and right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_MULTIPLY: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Left value must not be undef
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "* operator left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Right value Must not be undef
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "* operator right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Must be same type
                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "Type of * operator left and right value must be same at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                                                  
                  // Value must be int, long, float, double
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "Type of * operator left and right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_DIVIDE: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Left value must not be undef
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "/ operator left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Right value Must not be undef
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "/ operator right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Must be same type
                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "Type of / operator left and right value must be same at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                                                  
                  // Value must be int, long, float, double
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "Type of / operator left and right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_REMAINDER: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  SPVM_TYPE* last_type = SPVM_OP_get_type(compiler, op_cur->last);
                  
                  // Left value must not be undef
                  if (!first_type) {
                    SPVM_yyerror_format(compiler, "% operator left value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Right value Must not be undef
                  if (!last_type) {
                    SPVM_yyerror_format(compiler, "% operator right value must be not undef at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Must be same type
                  if (first_type->id != last_type->id) {
                    SPVM_yyerror_format(compiler, "Type of % operator left and right value must be same at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                                                  
                  // Value must be int, long, float, double
                  if (!SPVM_TYPE_is_numeric(compiler, first_type)) {
                    SPVM_yyerror_format(compiler, "Type of % operator left and right value must be core type at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_DIE: {
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur->first);
                  
                  if (!first_type || strcmp(first_type->name, "byte[]") != 0) {
                    SPVM_yyerror_format(compiler, "die argument type must be byte[] at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  break;
                }
                case SPVM_OP_C_CODE_PRE_INC:
                case SPVM_OP_C_CODE_POST_INC:
                case SPVM_OP_C_CODE_PRE_DEC:
                case SPVM_OP_C_CODE_POST_DEC: {
                  SPVM_OP* op_first = op_cur->first;
                  if (op_first->code != SPVM_OP_C_CODE_VAR) {
                    SPVM_yyerror_format(compiler, "invalid lvalue in increment at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_first);
                  
                  // Only int or long
                  if (first_type->id > SPVM_TYPE_C_ID_LONG) {
                    SPVM_yyerror_format(compiler, "Type of increment or decrement target must be integral at %s line %d\n", op_cur->file, op_cur->line);
                    break;
                  }
                  
                  op_cur->first->lvalue = 1;
                  
                  break;
                }
                // End of scope
                case SPVM_OP_C_CODE_BLOCK: {
                  
                  SPVM_OP* op_list_statement = op_cur->first;
                  
                  // Pop block my variable base
                  assert(block_my_var_base_stack->length > 0);
                  int32_t* block_my_var_base_ptr = SPVM_ARRAY_pop(block_my_var_base_stack);
                  block_my_var_base = *block_my_var_base_ptr;

                  // Pop loop block my variable base
                  if (op_cur->flag & SPVM_OP_C_FLAG_BLOCK_LOOP) {
                    assert(loop_block_my_var_base_stack->length > 0);
                    SPVM_ARRAY_pop(loop_block_my_var_base_stack);
                  }
                  // Pop try block my variable base
                  else if (op_cur->flag & SPVM_OP_C_FLAG_BLOCK_TRY) {
                    assert(try_block_my_var_base_stack->length > 0);
                    SPVM_ARRAY_pop(try_block_my_var_base_stack);
                  }
                  
                  // Free my variables at end of block
                  SPVM_OP* op_block_end = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_BLOCK_END, op_cur->file, op_cur->line);
                  
                  int32_t pop_count = op_my_var_stack->length - block_my_var_base;
                  for (int32_t j = 0; j < pop_count; j++) {
                    SPVM_OP* op_my_var = SPVM_ARRAY_pop(op_my_var_stack);
                    
                    SPVM_TYPE* type = SPVM_OP_get_type(compiler, op_my_var);
                    
                    // Decrement reference count at end of scope
                    if (!SPVM_TYPE_is_numeric(compiler, type)) {
                      SPVM_OP* op_dec_ref_count = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_DEC_REF_COUNT, op_cur->file, op_cur->line);
                      SPVM_OP* op_var = SPVM_OP_new_op_var_from_op_my_var(compiler, op_my_var);
                      SPVM_OP_sibling_splice(compiler, op_dec_ref_count, NULL, 0, op_var);
                      SPVM_OP_sibling_splice(compiler, op_block_end, NULL, 0, op_dec_ref_count);
                    }
                    
                    assert(op_my_var);
                  }
                  
                  if (!(op_cur->flag & SPVM_OP_C_FLAG_BLOCK_SUB)) {
                    SPVM_OP_sibling_splice(compiler, op_list_statement, op_list_statement->last, 0, op_block_end);
                  }
                  
                  if (block_my_var_base_stack->length > 0) {
                    int32_t* before_block_my_var_base_ptr = SPVM_ARRAY_fetch(block_my_var_base_stack, block_my_var_base_stack->length - 1);
                    int32_t before_block_my_var_base = *before_block_my_var_base_ptr;
                    block_my_var_base = before_block_my_var_base;
                  }
                  else {
                    block_my_var_base = 0;
                  }
                  
                  break;
                }
                // Add my var
                case SPVM_OP_C_CODE_VAR: {
                  
                  SPVM_VAR* var = op_cur->uv.var;
                  
                  // Search same name variable
                  SPVM_OP* op_my_var = NULL;
                  for (int32_t i = op_my_var_stack->length; i-- > 0; ) {
                    SPVM_OP* op_my_var_tmp = SPVM_ARRAY_fetch(op_my_var_stack, i);
                    SPVM_MY_VAR* my_var_tmp = op_my_var_tmp->uv.my_var;
                    if (strcmp(var->op_name->uv.name, my_var_tmp->op_name->uv.name) == 0) {
                      op_my_var = op_my_var_tmp;
                      break;
                    }
                  }
                  
                  if (op_my_var) {
                    // Add my var information to var
                    var->op_my_var = op_my_var;
                  }
                  else {
                    // Error
                    SPVM_yyerror_format(compiler, "%s is undeclared in this scope at %s line %d\n", var->op_name->uv.name, op_cur->file, op_cur->line);
                    compiler->fatal_error = 1;
                    return;
                  }
                  break;
                }
                case SPVM_OP_C_CODE_MY_VAR_INIT: {
                  
                  SPVM_OP* op_my_var = op_cur->first;
                  SPVM_MY_VAR* my_var = op_my_var->uv.my_var;
                  
                  // If argument my var is object, increment reference count
                  if (my_var->index < sub->op_args->length) {
                    SPVM_TYPE* type = SPVM_OP_get_type(compiler, op_my_var);
                    if (!SPVM_TYPE_is_numeric(compiler, type)) {
                      SPVM_OP* op_var = SPVM_OP_new_op_var_from_op_my_var(compiler, op_my_var);
                      
                      SPVM_OP* op_increfcount = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_INC_REF_COUNT, op_my_var->file, op_my_var->line);
                      SPVM_OP_sibling_splice(compiler, op_increfcount, NULL, 0, op_var);
                      SPVM_OP_sibling_splice(compiler, op_cur, op_cur->last, 0, op_increfcount);
                    }
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_MY_VAR: {
                  SPVM_MY_VAR* my_var = op_cur->uv.my_var;
                  
                  assert(my_var_length <= SPVM_LIMIT_C_MY_VARS);
                  if (my_var_length == SPVM_LIMIT_C_MY_VARS) {
                    SPVM_yyerror_format(compiler, "too many lexical variables, my \"%s\" ignored at %s line %d\n", my_var->op_name->uv.name, op_cur->file, op_cur->line);
                    compiler->fatal_error = 1;
                    break;
                  }
                  
                  // Search same name variable
                  _Bool found = 0;
                  
                  for (int32_t i = op_my_var_stack->length; i-- > block_my_var_base; ) {
                    SPVM_OP* op_bef_my_var = SPVM_ARRAY_fetch(op_my_var_stack, i);
                    SPVM_MY_VAR* bef_my_var = op_bef_my_var->uv.my_var;
                    if (strcmp(my_var->op_name->uv.name, bef_my_var->op_name->uv.name) == 0) {
                      found = 1;
                      break;
                    }
                  }
                  
                  if (found) {
                    SPVM_yyerror_format(compiler, "redeclaration of my \"%s\" at %s line %d\n", my_var->op_name->uv.name, op_cur->file, op_cur->line);
                    break;
                  }
                  else {
                    my_var->index = my_var_length++;
                    SPVM_ARRAY_push(op_my_vars, op_cur);
                    SPVM_ARRAY_push(op_my_var_stack, op_cur);
                  }
                  
                  // If left is object type and right is not exists, append "= undef" code
                  SPVM_TYPE* first_type = SPVM_OP_get_type(compiler, op_cur);
                  
                  // Assign undef if left value is object and right value is nothing
                  if (first_type && !SPVM_TYPE_is_numeric(compiler, first_type) && !SPVM_OP_sibling(compiler, op_cur)) {
                    // Only my declarations after subroutine arguments
                    if (my_var->index >= sub->op_args->length) {
                      SPVM_OP* op_assign = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_ASSIGN, op_cur->file, op_cur->line);
                      
                      SPVM_VAR* var = SPVM_VAR_new(compiler);
                      SPVM_OP* op_name_var = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_NAME, op_cur->file, op_cur->line);
                      op_name_var->uv.name = op_cur->uv.my_var->op_name->uv.name;
                      var->op_name = op_name_var;
                      var->op_my_var = op_cur;
                      
                      SPVM_OP* op_var = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_VAR, op_cur->file, op_cur->line);
                      op_var->uv.var = var;
                      
                      SPVM_OP* op_undef = SPVM_OP_new_op(compiler, SPVM_OP_C_CODE_UNDEF, op_cur->file, op_cur->line);
                      
                      SPVM_OP_sibling_splice(compiler, op_assign, op_assign->last, 0, op_var);
                      SPVM_OP_sibling_splice(compiler, op_assign, op_assign->last, 0, op_undef);
                      
                      SPVM_OP_sibling_splice(compiler, op_cur->sibparent, op_cur->sibparent->last, 0, op_assign);
                    }
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_CALL_SUB: {
                  
                  // Check sub name
                  SPVM_OP_resolve_sub_name(compiler, op_package, op_cur);
                  
                  const char* sub_abs_name = op_cur->uv.name_info->resolved_name;
                  
                  SPVM_OP* found_op_sub= SPVM_HASH_search(
                    compiler->op_sub_symtable,
                    sub_abs_name,
                    strlen(sub_abs_name)
                  );
                  if (!found_op_sub) {
                    SPVM_yyerror_format(compiler, "unknown sub \"%s\" at %s line %d\n",
                      sub_abs_name, op_cur->file, op_cur->line);
                    break;
                  }
                  
                  // Constant
                  SPVM_SUB* found_sub = found_op_sub->uv.sub;

                  int32_t sub_args_count = found_sub->op_args->length;
                  SPVM_OP* op_list_args = op_cur->last;
                  SPVM_OP* op_term = op_list_args->first;
                  int32_t call_sub_args_count = 0;
                  while ((op_term = SPVM_OP_sibling(compiler, op_term))) {
                    call_sub_args_count++;
                    if (call_sub_args_count > sub_args_count) {
                      SPVM_yyerror_format(compiler, "Too may arguments. sub \"%s\" at %s line %d\n", sub_abs_name, op_cur->file, op_cur->line);
                      return;
                    }
                    
                    _Bool is_invalid = 0;
                    
                    SPVM_OP* op_sub_arg_my_var = SPVM_ARRAY_fetch(found_sub->op_args, call_sub_args_count - 1);
                    
                    SPVM_TYPE* sub_arg_type = SPVM_OP_get_type(compiler, op_sub_arg_my_var);
                    
                    // Undef
                    if (op_term->code == SPVM_OP_C_CODE_UNDEF) {
                      if (SPVM_TYPE_is_numeric(compiler, sub_arg_type)) {
                        is_invalid = 1;
                      }
                    }
                    // Normal
                    else if (op_term) {
                      SPVM_TYPE* op_term_type = SPVM_OP_get_type(compiler, op_term);
                      
                      if (op_term_type->id !=  sub_arg_type->id) {
                        is_invalid = 1;
                      }
                    }
                    if (is_invalid) {
                      SPVM_yyerror_format(compiler, "Argument %d type is invalid. sub \"%s\" at %s line %d\n", (int) call_sub_args_count, sub_abs_name, op_cur->file, op_cur->line);
                      return;
                    }
                  }
                  
                  if (call_sub_args_count < sub_args_count) {
                    SPVM_yyerror_format(compiler, "Too few argument. sub \"%s\" at %s line %d\n", sub_abs_name, op_cur->file, op_cur->line);
                    return;
                  }
                  
                  // Constant subroutine
                  if (found_sub->is_constant) {
                    // Replace sub to constant
                    op_cur->code = SPVM_OP_C_CODE_CONSTANT;
                    op_cur->uv.constant = found_sub->op_block->uv.constant;
                    
                    op_cur->first = NULL;
                    op_cur->last = NULL;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_CALL_FIELD: {
                  SPVM_OP* op_term = op_cur->first;
                  SPVM_OP* op_name = op_cur->last;
                  
                  if (op_term->code != SPVM_OP_C_CODE_VAR
                    && op_term->code != SPVM_OP_C_CODE_ARRAY_ELEM
                    && op_term->code != SPVM_OP_C_CODE_CALL_FIELD
                    && op_term->code != SPVM_OP_C_CODE_CALL_SUB)
                  {
                    SPVM_yyerror_format(compiler, "field invoker is invalid \"%s\" at %s line %d\n",
                      op_name->uv.name, op_cur->file, op_cur->line);
                    compiler->fatal_error = 1;
                    break;
                  }
                  
                  // Check field name
                  SPVM_OP_resolve_field_name(compiler, op_cur);
                  
                  const char* field_abs_name = op_cur->uv.name_info->resolved_name;
                  
                  SPVM_OP* found_op_field= SPVM_HASH_search(
                    compiler->op_field_symtable,
                    field_abs_name,
                    strlen(field_abs_name)
                  );
                  if (!found_op_field) {
                    SPVM_yyerror_format(compiler, "unknown field \"%s\" at %s line %d\n",
                      field_abs_name, op_cur->file, op_cur->line);
                    compiler->fatal_error = 1;
                    break;
                  }
                  
                  break;
                }
                case SPVM_OP_C_CODE_CONVERT: {
                  
                  SPVM_OP* op_term = op_cur->first;
                  SPVM_OP* op_type = op_cur->last;
                  
                  SPVM_TYPE* op_term_type = SPVM_OP_get_type(compiler, op_term);
                  SPVM_TYPE* op_type_type = SPVM_OP_get_type(compiler, op_type);;
                  
                  // Can receive only core type
                  if (!SPVM_TYPE_is_numeric(compiler, op_term_type)) {
                    SPVM_yyerror_format(compiler, "can't convert type %s to %s at %s line %d\n",
                      op_term_type->name, op_type_type->name, op_cur->file, op_cur->line);
                    break;
                  }
                }
                break;
              }
              
              // [END]Postorder traversal position
              
              if (op_cur == op_base) {

                // Finish
                finish = 1;
                
                break;
              }
              
              // Next sibling
              if (op_cur->moresib) {
                op_cur = SPVM_OP_sibling(compiler, op_cur);
                break;
              }
              // Next is parent
              else {
                op_cur = op_cur->sibparent;
              }
            }
            if (finish) {
              break;
            }
          }
        }
        // Set my var information
        sub->op_my_vars = op_my_vars;
        
        // Operand stack max
        sub->operand_stack_max = op_count * 2;
      }
      
      // Push sub name to constant pool
      sub->abs_name_constant_pool_index = compiler->constant_pool->length;
      SPVM_CONSTANT_POOL_push_string(compiler, compiler->constant_pool, sub->abs_name);
      
      // Push file name to constant pool
      sub->file_name_constant_pool_index = compiler->constant_pool->length;
      assert(sub->file_name);
      SPVM_CONSTANT_POOL_push_string(compiler, compiler->constant_pool, sub->file_name);
      
      // Push sub information to constant pool
      sub->constant_pool_index = compiler->constant_pool->length;
      SPVM_CONSTANT_POOL_push_sub(compiler, compiler->constant_pool, sub);
    }
  }
}
