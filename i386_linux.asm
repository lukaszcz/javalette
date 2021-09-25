
        section .text
        global _start
        extern printf, scanf, exit
_start:
        call main
        push eax
        call exit

error:
        push __error_str
        call printf
        add esp, 4
        push byte 1
        call exit
        
readInt:
        push byte 0
        push esp
        push __int_format
        call scanf
        add esp, 12
        mov eax, [esp - 4]
        ret

readDouble:
        lea eax, [esp + 4]
        push eax
        push __double_format
        call scanf
        add esp, 8
        mov eax, [esp - 4]
        ret

printInt:
        push dword [esp + 4]
        push __int_format
        call printf
        add esp, 8
        ret 4

printDouble:
        push dword [esp + 8]
        push dword [esp + 8]
        push __double_format
        call printf
        add esp, 12
        ret 8

printString:
        push dword [esp + 4]
        call printf
        add esp, 4
        ret 4

        section .data
__error_str     db "runtime error",10,0
__double_format db "%f",10,0
__int_format    db "%d",10,0
