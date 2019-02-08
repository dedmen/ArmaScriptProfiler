option casemap :none

_TEXT    SEGMENT
    ;https://msdn.microsoft.com/en-us/library/windows/hardware/ff561499(v=vs.85).aspx

    ;mangled functions
    EXTERN ?shouldTime@PCounter@@QEAA_NXZ:              PROC;   PCounter::shouldTime
    EXTERN ?doEnd@ScopeProf@@QEAAXXZ:					PROC;   ScopeProf::doEnd
    EXTERN ?scopeCompleted@ArmaProf@@QEAAX_J0PEAVr_string@types@intercept@@PEAVPCounter@@@Z:					PROC;   ArmaProf::scopeCompleted
    EXTERN ?frameEnd@ArmaProf@@QEAAXMMH@Z:				PROC;   ArmaProf::frameEnd
    EXTERN insertCompileCache:							PROC;
	
    ;JmpBacks
    EXTERN profEndJmpback:								qword
    EXTERN compileCacheInsJmpback:						qword

    ;##########
    PUBLIC shouldTime
    shouldTime PROC
        jmp ?shouldTime@PCounter@@QEAA_NXZ
    shouldTime ENDP

	
    ;##########
    PUBLIC doEnd
    doEnd PROC
		;pop rax; fix tainting from hook


        ;push RAX;												Overkill.. I know..
        push RCX 
        push RDX 
        push R8 
        push R9 
        push R10
        push R11

        call    ?doEnd@ScopeProf@@QEAAXXZ;						ScopeProf::doEnd

        pop R11
        pop R10 
        pop R9 
        pop R8 
        pop RDX 
        pop RCX 
        pop RAX 

		;fixup
        push    rbx
		sub     rsp, 30h     
		cmp     byte ptr [rcx+11h], 0
        mov     rbx, rcx
                                     
        jmp     profEndJmpback;

    doEnd ENDP

	;##########
    PUBLIC scopeCompleted
    scopeCompleted PROC
        jmp     ?scopeCompleted@ArmaProf@@QEAAX_J0PEAVr_string@types@intercept@@PEAVPCounter@@@Z;
    scopeCompleted ENDP

	;##########
    PUBLIC frameEnd
    frameEnd PROC
        jmp ?frameEnd@ArmaProf@@QEAAXMMH@Z
    frameEnd ENDP


	;##########
    PUBLIC compileCacheIns
    compileCacheIns PROC


		;//rbp-60h compiled
		;//rbx source
		;These happen to be free so why not use them right away? :D
		mov rcx, rbp ;code
		sub rcx, 60h
		mov rdx, rbx ;pos


		push rax;
		push rbx;
		;push rcx;
		;push rdx;
		push r8;
		push r9;
		push r10;
		push r11;
		push r11; Don't ask me. Just do
		push r11;



		call insertCompileCache;

		pop r11;
		pop r11;
		pop r11;
		pop r10;
		pop r9;
		pop r8;
		;pop rdx;
		;pop rcx;
		pop rbx;
		pop rax;

	; fixup

		mov     [rbp-50h], rax
		mov     eax, [rbx+10h]
		mov     [rbp-48h], eax
		mov     rax, [rbx+18h]

        jmp compileCacheInsJmpback
    compileCacheIns ENDP





_TEXT    ENDS
END
