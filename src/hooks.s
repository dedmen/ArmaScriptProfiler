.intel_syntax noprefix
.global	_start


.text
	###########
	.global shouldTime
    shouldTime:
        jmp _ZN8PCounter10shouldTimeEv

	###########
	.global scopeCompleted
    scopeCompleted:
        jmp     _ZN8ArmaProf14scopeCompletedExxPvPx
