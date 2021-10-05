
        section .text
        global _start
_start:
        call main
        mov ebx, eax
        mov eax, 1              ; exit()
        int 0x80

error:
        mov edx, [__error_str_len]
        mov ecx, __error_str
        mov ebx, 2
        mov eax, 4              ; write()
        int 0x80
        mov ebx, 1
        mov eax, 1              ; exit()
        int 0x80
        
readInt:
        call __read_line
        xor eax, eax
.l1:
        lea eax, [eax + 4 * eax]
        add eax, eax
        

readDouble:
        

printInt:

printDouble:
        finit
        fld qword [__10]
        fld qword [esp + 4]
        

printString:
        mov ecx, [esp + 4]
        mov edx, ecx
        jmp .l2
.l1:
        inc edx
.l2:
        cmp byte [edx], 0
        jnz .l1
        sub edx, ecx
        mov ebx, 1
        mov eax, 4              ; write()
        int 0x80
        ret 4

__read_line:
        mov edi, __line
        mov esi, [__buffer_ptr]
        mov ecx, [__buffer_end]
        jmp .l2
.l1:
        mov al, [esi]
        cmp al, '\n'
        je .out
        mov [edi], al
        inc esi
        inc edi
        cmp edi, __line + 256
        je .out
        dec ecx
        jnz .l1
.l2:
        sub ecx, esi
        ja .l1
        call __read
        mov edi, __line
        mov esi, [__buffer_ptr]
        mov ecx, [__buffer_end]
        jmp .l2
.out:
        mov [__line_end], edi
        ret

__read:
        mov edx, 256
        mov ecx, __buffer
        mov ebx, 0
        mov eax, 3              ; read()
        int 0x80
        cmp eax, -1
        je near error
        add eax, __buffer
        mov [__buffer_end], eax
        mov [__buffer_ptr], 0
        ret
        

        section .data
__error_str_len dd 13
__line_end      dd 0
__buffer_ptr    dd 0
__buffer_end    dd 0
__error_str     db "runtime error"
__buffer        resb 256
__line          resb 256

