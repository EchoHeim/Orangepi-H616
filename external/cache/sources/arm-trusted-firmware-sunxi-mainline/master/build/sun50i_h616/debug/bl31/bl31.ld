OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(bl31_entrypoint)
MEMORY {
    RAM (rwx): ORIGIN = 0x40000000, LENGTH = (0x40000000 + 0x40000) - 0x40000000
}
SECTIONS
{
    . = 0x40000000;
    ASSERT(. == ALIGN(((1) << (12))),
           "BL31_BASE address is not aligned on a page boundary.")
    __BL31_START__ = .;
    .text . : {
        __TEXT_START__ = .;
        *bl31_entrypoint.o(.text*)
        *(SORT_BY_ALIGNMENT(SORT(.text*)))
        *(.vectors)
        . = ALIGN(((1) << (12)));
        __TEXT_END__ = .;
    } >RAM
    .rodata . : {
        __RODATA_START__ = .;
        *(SORT_BY_ALIGNMENT(.rodata*))
 . = ALIGN(8); __RT_SVC_DESCS_START__ = .; KEEP(*(rt_svc_descs)) __RT_SVC_DESCS_END__ = .; . = ALIGN(8); __FCONF_POPULATOR_START__ = .; KEEP(*(.fconf_populator)) __FCONF_POPULATOR_END__ = .; . = ALIGN(8); __PMF_SVC_DESCS_START__ = .; KEEP(*(pmf_svc_descs)) __PMF_SVC_DESCS_END__ = .; . = ALIGN(8); __PARSER_LIB_DESCS_START__ = .; KEEP(*(.img_parser_lib_descs)) __PARSER_LIB_DESCS_END__ = .; . = ALIGN(8); __CPU_OPS_START__ = .; KEEP(*(cpu_ops)) __CPU_OPS_END__ = .; . = ALIGN(8); __GOT_START__ = .; *(.got) __GOT_END__ = .;
        . = ALIGN(8);
__pubsub_psci_cpu_on_finish_start = .; KEEP(*(__pubsub_psci_cpu_on_finish)); __pubsub_psci_cpu_on_finish_end = .;
__pubsub_psci_suspend_pwrdown_start_start = .; KEEP(*(__pubsub_psci_suspend_pwrdown_start)); __pubsub_psci_suspend_pwrdown_start_end = .;
__pubsub_psci_suspend_pwrdown_finish_start = .; KEEP(*(__pubsub_psci_suspend_pwrdown_finish)); __pubsub_psci_suspend_pwrdown_finish_end = .;
__pubsub_cm_entering_secure_world_start = .; KEEP(*(__pubsub_cm_entering_secure_world)); __pubsub_cm_entering_secure_world_end = .;
__pubsub_cm_exited_secure_world_start = .; KEEP(*(__pubsub_cm_exited_secure_world)); __pubsub_cm_exited_secure_world_end = .;
__pubsub_cm_entering_normal_world_start = .; KEEP(*(__pubsub_cm_entering_normal_world)); __pubsub_cm_entering_normal_world_end = .;
__pubsub_cm_exited_normal_world_start = .; KEEP(*(__pubsub_cm_exited_normal_world)); __pubsub_cm_exited_normal_world_end = .;
        . = ALIGN(((1) << (12)));
        __RODATA_END__ = .;
    } >RAM
    ASSERT(__CPU_OPS_END__ > __CPU_OPS_START__,
           "cpu_ops not defined for this platform.")
    __RW_START__ = . ;
    .data . : ALIGN(1) { __DATA_START__ = .; *(SORT_BY_ALIGNMENT(.data*)) __DATA_END__ = .; } >RAM
    .rela.dyn : ALIGN(8) { __RELA_START__ = .; *(.rela*) __RELA_END__ = .; } >RAM
    stacks (NOLOAD) : { __STACKS_START__ = .; *(tzfw_normal_stacks) __STACKS_END__ = .; } >RAM
    .bss (NOLOAD) : ALIGN(16) { __BSS_START__ = .; *(SORT_BY_ALIGNMENT(.bss*)) *(COMMON) . = ALIGN((1 << 6)); __PMF_TIMESTAMP_START__ = .; KEEP(*(pmf_timestamp_array)) . = ALIGN((1 << 6)); __PMF_PERCPU_TIMESTAMP_END__ = .; __PERCPU_TIMESTAMP_SIZE__ = ABSOLUTE(. - __PMF_TIMESTAMP_START__); . = . + (__PERCPU_TIMESTAMP_SIZE__ * (((1) * (4)) - 1)); __PMF_TIMESTAMP_END__ = .; . = ALIGN(16); __BASE_XLAT_TABLE_START__ = .; *(base_xlat_table) __BASE_XLAT_TABLE_END__ = .; __BSS_END__ = .; } >RAM
    xlat_table (NOLOAD) : { __XLAT_TABLE_START__ = .; *(xlat_table) __XLAT_TABLE_END__ = .; } >RAM
    coherent_ram (NOLOAD) : ALIGN(((1) << (12))) {
        __COHERENT_RAM_START__ = .;
        *(bakery_lock)
        *(tzfw_coherent_mem)
        __COHERENT_RAM_END_UNALIGNED__ = .;
        . = ALIGN(((1) << (12)));
        __COHERENT_RAM_END__ = .;
    } >RAM
    __RW_END__ = .;
    __BL31_END__ = .;
    /DISCARD/ : {
        *(.dynsym .dynstr .hash .gnu.hash)
    }
    ASSERT(. <= (0x40000000 + 0x40000), "BL31 image has exceeded its limit.")
}
