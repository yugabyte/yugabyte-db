/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Vixie.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)bitstring.h	5.2 (Berkeley) 4/4/90
 */

typedef	unsigned char bitstr_t;

/* internal macros */
				/* byte of the bitstring bit is in */
#define	_bit_byte(bit) \
	((bit) >> 3)

				/* mask for the bit within its byte */
#define	_bit_mask(bit) \
	(1 << ((bit)&0x7))

/* external macros */
				/* bytes in a bitstring of nbits bits */
#define	bitstr_size(nbits) \
	((((nbits) - 1) >> 3) + 1)

				/* allocate a bitstring */
#define	bit_alloc(nbits) \
	(bitstr_t *)malloc(1, \
	    (unsigned int)bitstr_size(nbits) * sizeof(bitstr_t))

				/* allocate a bitstring on the stack */
#define	bit_decl(name, nbits) \
	(name)[bitstr_size(nbits)]

				/* is bit N of bitstring name set? */
#define	bit_test(name, bit) \
	((name)[_bit_byte(bit)] & _bit_mask(bit))

				/* set bit N of bitstring name */
#define	bit_set(name, bit) \
	(name)[_bit_byte(bit)] |= _bit_mask(bit)

				/* clear bit N of bitstring name */
#define	bit_clear(name, bit) \
	(name)[_bit_byte(bit)] &= ~_bit_mask(bit)

				/* clear bits start ... stop in bitstring */
#define	bit_nclear(name, start, stop) { \
	register bitstr_t *_name = name; \
	register int _start = start, _stop = stop; \
	register int _startbyte = _bit_byte(_start); \
	register int _stopbyte = _bit_byte(_stop); \
	if (_startbyte == _stopbyte) { \
		_name[_startbyte] &= ((0xff >> (8 - (_start&0x7))) | \
				      (0xff << ((_stop&0x7) + 1))); \
	} else { \
		_name[_startbyte] &= 0xff >> (8 - (_start&0x7)); \
		while (++_startbyte < _stopbyte) \
			_name[_startbyte] = 0; \
		_name[_stopbyte] &= 0xff << ((_stop&0x7) + 1); \
	} \
}

				/* set bits start ... stop in bitstring */
#define	bit_nset(name, start, stop) { \
	register bitstr_t *_name = name; \
	register int _start = start, _stop = stop; \
	register int _startbyte = _bit_byte(_start); \
	register int _stopbyte = _bit_byte(_stop); \
	if (_startbyte == _stopbyte) { \
		_name[_startbyte] |= ((0xff << (_start&0x7)) & \
				    (0xff >> (7 - (_stop&0x7)))); \
	} else { \
		_name[_startbyte] |= 0xff << ((_start)&0x7); \
		while (++_startbyte < _stopbyte) \
	    		_name[_startbyte] = 0xff; \
		_name[_stopbyte] |= 0xff >> (7 - (_stop&0x7)); \
	} \
}

				/* find first bit clear in name */
#define	bit_ffc(name, nbits, value) { \
	register bitstr_t *_name = name; \
	register int _byte, _nbits = nbits; \
	register int _stopbyte = _bit_byte(_nbits), _value = -1; \
	for (_byte = 0; _byte <= _stopbyte; ++_byte) \
		if (_name[_byte] != 0xff) { \
			_value = _byte << 3; \
			for (_stopbyte = _name[_byte]; (_stopbyte&0x1); \
			    ++_value, _stopbyte >>= 1); \
			break; \
		} \
	*(value) = _value; \
}

				/* find first bit set in name */
#define	bit_ffs(name, nbits, value) { \
	register bitstr_t *_name = name; \
	register int _byte, _nbits = nbits; \
	register int _stopbyte = _bit_byte(_nbits), _value = -1; \
	for (_byte = 0; _byte <= _stopbyte; ++_byte) \
		if (_name[_byte]) { \
			_value = _byte << 3; \
			for (_stopbyte = _name[_byte]; !(_stopbyte&0x1); \
			    ++_value, _stopbyte >>= 1); \
			break; \
		} \
	*(value) = _value; \
}
