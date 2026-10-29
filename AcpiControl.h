/* Fexox OS Project, EULA GPLv2 */
#ifndef ACPI_CONTROL_H
#define ACPI_CONTROL_H

#include <stdint.h>



int acpi_init(uint64_t rsdp_phys);



int acpi_power_off(void);

 

int acpi_reset(void);

 

void cpu_triple_fault_reset(void);

#endif  
