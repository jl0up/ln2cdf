/*
 * ln2cdf project
 *
 * by jl0up
 * 
 * https://github.com/jl0up/ln2cdf
 * 
 */

#include "chip_info.h"
#include "delayed_restart.h"

void app_main(void)
{
    print_chip_information();


    
    delayed_restart();
}
