#! /usr/bin/env perl
# Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


$flavour = shift;
$output  = shift;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
( $xlate="${dir}perlasm/arm-xlate.pl" and -f $xlate) or
die "can't locate arm-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

$code.=<<___;
#include "arm_arch.h"

.text
.arch	armv8-a+crypto

.align	5
.globl	_armv7_neon_probe
.type	_armv7_neon_probe,%function
_armv7_neon_probe:
	orr	v15.16b, v15.16b, v15.16b
	ret
.size	_armv7_neon_probe,.-_armv7_neon_probe

.globl	_armv7_tick
.type	_armv7_tick,%function
_armv7_tick:
#ifdef	__APPLE__
	mrs	x0, CNTPCT_EL0
#else
	mrs	x0, CNTVCT_EL0
#endif
	ret
.size	_armv7_tick,.-_armv7_tick

.globl	_armv8_aes_probe
.type	_armv8_aes_probe,%function
_armv8_aes_probe:
	aese	v0.16b, v0.16b
	ret
.size	_armv8_aes_probe,.-_armv8_aes_probe

.globl	_armv8_sha1_probe
.type	_armv8_sha1_probe,%function
_armv8_sha1_probe:
	sha1h	s0, s0
	ret
.size	_armv8_sha1_probe,.-_armv8_sha1_probe

.globl	_armv8_sha256_probe
.type	_armv8_sha256_probe,%function
_armv8_sha256_probe:
	sha256su0	v0.4s, v0.4s
	ret
.size	_armv8_sha256_probe,.-_armv8_sha256_probe
.globl	_armv8_pmull_probe
.type	_armv8_pmull_probe,%function
_armv8_pmull_probe:
	pmull	v0.1q, v0.1d, v0.1d
	ret
.size	_armv8_pmull_probe,.-_armv8_pmull_probe

.globl	OPENSSL_cleanse
.type	OPENSSL_cleanse,%function
.align	5
OPENSSL_cleanse:
	cbz	x1,.Lret	// len==0?
	cmp	x1,#15
	b.hi	.Lot		// len>15
	nop
.Little:
	strb	wzr,[x0],#1	// store byte-by-byte
	subs	x1,x1,#1
	b.ne	.Little
.Lret:	ret

.align	4
.Lot:	tst	x0,#7
	b.eq	.Laligned	// inp is aligned
	strb	wzr,[x0],#1	// store byte-by-byte
	sub	x1,x1,#1
	b	.Lot

.align	4
.Laligned:
	str	xzr,[x0],#8	// store word-by-word
	sub	x1,x1,#8
	tst	x1,#-8
	b.ne	.Laligned	// len>=8
	cbnz	x1,.Little	// len!=0?
	ret
.size	OPENSSL_cleanse,.-OPENSSL_cleanse

.globl	CRYPTO_memcmp
.type	CRYPTO_memcmp,%function
.align	4
CRYPTO_memcmp:
	eor	w3,w3,w3
	cbz	x2,.Lno_data	// len==0?
.Loop_cmp:
	ldrb	w4,[x0],#1
	ldrb	w5,[x1],#1
	eor	w4,w4,w5
	orr	w3,w3,w4
	subs	x2,x2,#1
	b.ne	.Loop_cmp

.Lno_data:
	neg	w0,w3
	lsr	w0,w0,#31
	ret
.size	CRYPTO_memcmp,.-CRYPTO_memcmp
___

print $code;
close STDOUT;
