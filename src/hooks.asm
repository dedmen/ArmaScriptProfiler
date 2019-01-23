option casemap :none

_TEXT    SEGMENT
    ;https://msdn.microsoft.com/en-us/library/windows/hardware/ff561499(v=vs.85).aspx

    ;mangled functions
    EXTERN ?shouldTime@PCounter@@QEAA_NXZ:              PROC;   PCounter::shouldTime
    EXTERN ?doEnd@ScopeProf@@QEAAXXZ:					PROC;   ScopeProf::doEnd

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
        push RAX;												Overkill.. I know..
        push RDX 
        push R8 
        push R9 
        push R10; //#TODO R10 and R11 probably not needed
        push R11

        call    ?doEnd@ScopeProf@@QEAAXXZ;						ScopeProf::doEnd

        pop R11
        pop R10 
        pop R9 
        pop R8 
        pop RDX 
        pop RAX 

		;fixup
        push    rbx
		sub     rsp, 30h
		cmp     byte ptr [rcx+11h], 0
		mov     rbx, rcx
         
        
                                     
        jmp     profEndJmpback;

    doEnd ENDP



_TEXT    ENDS
END
