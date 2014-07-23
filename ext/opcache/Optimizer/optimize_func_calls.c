/* pass 4
 * - optimize INIT_FCALL_BY_NAME to DO_FCALL
 */
#if ZEND_EXTENSION_API_NO > PHP_5_3_X_API_NO

typedef struct _optimizer_call_info {
	zend_function *func;
	zend_op       *opline;
} optimizer_call_info;

static void optimize_func_calls(zend_op_array *op_array, zend_optimizer_ctx *ctx TSRMLS_DC) {
	zend_op *opline = op_array->opcodes;
	zend_op *end = opline + op_array->last;
	int call = 0;
	void *checkpoint; 
	optimizer_call_info *call_stack;

	if (op_array->last < 2) {
		return;
	}

	checkpoint = zend_arena_checkpoint(ctx->arena); 
	call_stack = zend_arena_calloc(&ctx->arena, op_array->last / 2, sizeof(optimizer_call_info));
	while (opline < end) {
		switch (opline->opcode) {
			case ZEND_INIT_FCALL_BY_NAME:
			case ZEND_INIT_NS_FCALL_BY_NAME:
				if (ZEND_OP2_TYPE(opline) == IS_CONST) {
					zend_function *func;
					zval *function_name = &op_array->literals[opline->op2.constant + 1];
					if ((func = zend_hash_find_ptr(&ctx->script->function_table,
							Z_STR_P(function_name))) != NULL) {
						call_stack[call].func = func;
					}
				}
				/* break missing intentionally */
			case ZEND_NEW:
			case ZEND_INIT_METHOD_CALL:
			case ZEND_INIT_STATIC_METHOD_CALL:
			case ZEND_INIT_FCALL:
			case ZEND_INIT_USER_CALL:
				call_stack[call].opline = opline;
				call++;
				break;
			case ZEND_DO_FCALL:
				call--;
				if (call_stack[call].func && call_stack[call].opline) {
					zend_op *fcall = call_stack[call].opline;

					if (fcall->opcode == ZEND_INIT_FCALL_BY_NAME) {
						fcall->opcode = ZEND_INIT_FCALL;
						Z_CACHE_SLOT(op_array->literals[fcall->op2.constant + 1]) = Z_CACHE_SLOT(op_array->literals[fcall->op2.constant]);
						literal_dtor(&ZEND_OP2_LITERAL(fcall));
						fcall->op2.constant = fcall->op2.constant + 1;
					} else if (fcall->opcode == ZEND_INIT_NS_FCALL_BY_NAME) {
						fcall->opcode = ZEND_INIT_FCALL;
						Z_CACHE_SLOT(op_array->literals[fcall->op2.constant + 1]) = Z_CACHE_SLOT(op_array->literals[fcall->op2.constant]);
						literal_dtor(&op_array->literals[fcall->op2.constant]);
						literal_dtor(&op_array->literals[fcall->op2.constant + 2]);
						fcall->op2.constant = fcall->op2.constant + 1;
					} else {
						ZEND_ASSERT(0);
					}
				} else if (opline->extended_value == 0 &&
				           call_stack[call].opline &&
				           call_stack[call].opline->opcode == ZEND_INIT_FCALL_BY_NAME &&
				           ZEND_OP2_TYPE(call_stack[call].opline) == IS_CONST) {

					zend_op *fcall = call_stack[call].opline;

					fcall->opcode = ZEND_INIT_FCALL;
					Z_CACHE_SLOT(op_array->literals[fcall->op2.constant + 1]) = Z_CACHE_SLOT(op_array->literals[fcall->op2.constant]);
					literal_dtor(&ZEND_OP2_LITERAL(fcall));
					fcall->op2.constant = fcall->op2.constant + 1;
				}
				call_stack[call].func = NULL;
				call_stack[call].opline = NULL;
				break;
			case ZEND_FETCH_FUNC_ARG:
			case ZEND_FETCH_OBJ_FUNC_ARG:
			case ZEND_FETCH_DIM_FUNC_ARG:
				if (call_stack[call - 1].func) {
					if (ARG_SHOULD_BE_SENT_BY_REF(call_stack[call - 1].func, (opline->extended_value & ZEND_FETCH_ARG_MASK))) {
						opline->extended_value = 0;
						opline->opcode -= 9;
					} else {
						opline->extended_value = 0;
						opline->opcode -= 12;
					}
				}
				break;
			case ZEND_SEND_VAL_EX:
				if (call_stack[call - 1].func) {
					if (ARG_MUST_BE_SENT_BY_REF(call_stack[call - 1].func, opline->op2.num)) {
						/* We won't convert it into_DO_FCALL to emit error at run-time */
						call_stack[call - 1].opline = NULL;
					} else {
						opline->opcode = ZEND_SEND_VAL;
					}
				}
				break;
			case ZEND_SEND_VAR_EX:
				if (call_stack[call - 1].func) {
					if (ARG_SHOULD_BE_SENT_BY_REF(call_stack[call - 1].func, opline->op2.num)) {
						opline->opcode = ZEND_SEND_REF;
					} else {
						opline->opcode = ZEND_SEND_VAR;
					}
				}
				break;
			case ZEND_SEND_VAR_NO_REF:
				if (!(opline->extended_value & ZEND_ARG_COMPILE_TIME_BOUND) && call_stack[call - 1].func) {
					if (ARG_SHOULD_BE_SENT_BY_REF(call_stack[call - 1].func, opline->op2.num)) {
						opline->extended_value |= ZEND_ARG_COMPILE_TIME_BOUND | ZEND_ARG_SEND_BY_REF;
					} else if (opline->extended_value) {
						opline->extended_value |= ZEND_ARG_COMPILE_TIME_BOUND;
					} else {
						opline->opcode = ZEND_SEND_VAR;
						opline->extended_value = 0;
					}
				}
				break;
#if 0
			case ZEND_SEND_REF:
				if (opline->extended_value != ZEND_ARG_COMPILE_TIME_BOUND && call_stack[call - 1].func) {
					/* We won't handle run-time pass by reference */
					call_stack[call - 1].opline = NULL;
				}
				break;
#endif
#if ZEND_EXTENSION_API_NO > PHP_5_5_X_API_NO
			case ZEND_SEND_UNPACK:
				call_stack[call - 1].func = NULL;
				call_stack[call - 1].opline = NULL;
				break;
#endif
			default:
				break;
		}
		opline++;
	}

	zend_arena_release(&ctx->arena, checkpoint);
}
#endif