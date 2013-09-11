/*
 * status.h
 *
 *  Created on: Apr 3, 2013
 *      Author: ashish
 */

#ifndef STATUS_H_
#define STATUS_H_

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <uhd/exception.hpp>

#define ENABLE_EXTENDED_ERROR_INFO false

typedef int32_t nirio_status;

namespace nirio_interface {
struct nirio_err_info {
    nirio_err_info(nirio_status arg_code, const char* arg_msg): code(arg_code), msg(arg_msg) {}

    nirio_status code;
    const char* msg;

    static const nirio_err_info NIRIO_ERROR_TABLE[];
    static const size_t NIRIO_ERROR_TABLE_SIZE;
};

const std::string lookup_err_msg(nirio_status code);

void nirio_status_to_exception(const nirio_status& status, const std::string& message);
}

#define nirio_status_fatal(status) ((status) < 0)
#define nirio_status_not_fatal(status) ((status) >= 0)

#define nirio_status_chain(func, status) 	\
	if (nirio_status_not_fatal(status)) {	\
		status = (func);					\
		if (ENABLE_EXTENDED_ERROR_INFO && nirio_status_fatal(status)) {	\
			fprintf(stderr,"ERROR: The following function call returned status code %d\n%s\n%s:%d\n",status,#func,__FILE__,__LINE__);	\
		}									\
	}										\


#define NIRIO_ERR_INFO(CONST_NAME, ERR_CODE, ERR_MSG) \
    static const nirio_status CONST_NAME = ERR_CODE;
#include "nirio_err_template.h"
#undef NIRIO_ERR_INFO

#endif /* STATUS_H_ */
