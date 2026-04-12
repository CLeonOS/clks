.intel_syntax noprefix

.global clks_exec_call_on_stack_x86_64
.type clks_exec_call_on_stack_x86_64, @function

clks_exec_call_on_stack_x86_64:
    mov r11, rsp
    mov rsp, rsi
    and rsp, -16
    sub rsp, 8
    mov [rsp], r11

    call rdi

    mov rsp, [rsp]
    ret

.size clks_exec_call_on_stack_x86_64, .-clks_exec_call_on_stack_x86_64
.section .note.GNU-stack,"",@progbits
