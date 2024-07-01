#ifndef _HYPERCALL_H_
#define _HYPERCALL_H_

//hypercall is COP3|1|'dgv' thus: 0x4f646776 -> 'Odgv'
//call number is in $at, params in $a0, $a1, $a2, $a3, return in $v0, $v1



#define HYPERCALL			0x4f646776
#define H_GET_MEM_MAP		0
#define H_CONSOLE_WRITE		1
#define H_STOR_GET_SZ		2
#define H_STOR_READ			3
#define H_STOR_WRITE		4
#define H_TERM				5

/*
calls:

	0	GET_MEM_MAP						param is index. ret: [0] is num bits in map, [1] is how much ram each bit means, [2+] are ram map bytes
	1	CONSOLE_WRITE(u8 val)			send a char to console (for debugging)
	2	STOR_GET_SZ						return u32 stor_sz_in_512B_blocks
	3	STOR_READ(u32 block, u32 pa)	reada a storage block to a given PA. result is a bool
	4	STOR_WRITE(u32 block, u32 pa)	writes a block to disk from a given PA. result is a bool
	5	TERM							terminate emulation
*/


#endif