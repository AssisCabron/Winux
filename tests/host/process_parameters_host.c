#include "../../src/process/process_parameters.h"
__declspec(dllexport) uint32_t build_parameters(void *output,size_t capacity,const pp_input *input,size_t *required){
 return pp_build(output,capacity,input,required);
}
