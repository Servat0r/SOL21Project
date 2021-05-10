#if !defined(_NUM_FUNCS_H)

#define _NUM_FUNCS_H
#include <defines.h>
	
bool 
	isNumber(char* str),
	isFPNumber(char* str);

int
	getInt(char* str, long* val),
	getFloat(char* str, float* val);

#endif
