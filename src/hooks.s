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
        jmp     _ZN8ArmaProf14scopeCompletedExxPN9intercept5types8r_stringEP8PCounter

	.global frameEnd
    frameEnd:
        jmp     _ZN8ArmaProf8frameEndEffi