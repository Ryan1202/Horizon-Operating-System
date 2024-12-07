#ifndef _RESULT_H
#define _RESULT_H

#define RESULT_DELIVER_CALL(result_type, result_ok, func, add_ops, ...) \
	{                                                                   \
		result_type result = func(__VA_ARGS__);                         \
		if (result != result_ok) {                                      \
			add_ops;                                                    \
			return result;                                              \
		}                                                               \
	}

#define RESULT_CASE_PRINT(result) \
	case result:                  \
		printk(#result);          \
		break;

#endif