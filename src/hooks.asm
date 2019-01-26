option casemap :none

_TEXT    SEGMENT
    ;https://msdn.microsoft.com/en-us/library/windows/hardware/ff561499(v=vs.85).aspx

    ;mangled functions
    EXTERN ?shouldTime@PCounter@@QEAA_NXZ:              PROC;   PCounter::shouldTime
    EXTERN ?doEnd@ScopeProf@@QEAAXXZ:					PROC;   ScopeProf::doEnd
    EXTERN ?scopeCompleted@ArmaProf@@QEAAX_J0PEAVr_string@types@intercept@@PEAVPCounter@@@Z:					PROC;   ArmaProf::scopeCompleted

    ;JmpBacks
    EXTERN profEndJmpback:								qword

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


_TEXT    ENDS
END
