#！ /usr/bin/env python

import extargsparse
import sys
import cmdpack
import logging
import re


def set_logging(args):
	loglvl= logging.ERROR
	if args.verbose >= 3:
		loglvl = logging.DEBUG
	elif args.verbose >= 2:
		loglvl = logging.INFO
	if logging.root is not None and len(logging.root.handlers) > 0:
		logging.root.handlers = []
	logging.basicConfig(level=loglvl,format='%(asctime)s:%(filename)s:%(funcName)s:%(lineno)d\t%(message)s')
	return

def gitsvnfetch_handler(args,parser):
	set_logging(args)
	matchexpr = re.compile('^r([0-9]+)\\s+',re.I)
	lastnum = None
	exitcode = 1
	cmds = ['git','svn','fetch']
	while True:
		logging.info('run %s'%(cmds))
		printouted = False
		p = cmdpack.run_cmd_output(cmds)
		for l in p:
			l = l.rstrip('\r\n')
			logging.info('%s'%(l))
			m = matchexpr.findall(l)
			if m is not None and len(m) > 0 :
				lastnum = int(m[0])
				if not printouted:
					printouted = True
					sys.stdout.write('%d'%(lastnum))
					sys.stdout.flush()
				if (lastnum % 10) == 0:
					sys.stdout.write('.')
					sys.stdout.flush()
				if (lastnum % 100) == 0:
					sys.stdout.write('\n')
		attr = cmdpack.CmdObjectAttr()
		attr.maxwtime = 0.1
		exitcode = p.get_exitcode(attr)
		if lastnum is not None:
			sys.stderr.write('fetch  [%s] [%d]\n'%(lastnum,exitcode))
		if exitcode == 0:
			break
		elif args.failfast:
			break
	if exitcode == 0:
		sys.stdout.write('gitsvnfetch succ\n')
	sys.exit(0)
	return

def runsucc_handler(args,parser):
	set_logging(args)
	cmds = args.subnargs
	lastlen = 0
	trycnt = 0
	while True:
		p = cmdpack.run_cmd_output(cmds,True,True)
		for l in p:
			l = l.rstrip('\r\n') 
			logging.info('get [%s]'%(l))
			if lastlen > 0:
				for i in range(lastlen):
					sys.stdout.write('\b')
				sys.stdout.flush()
			sys.stdout.write('%s'%(l))
			sys.stdout.flush()
			lastlen = len(l)
		exitcode = p.get_exitcode()
		if exitcode == 0:
			sys.stdout.write('\n')
			break
		if lastlen > 0:
			for i in range(lastlen):
				sys.stdout.write('\b')
			sys.stdout.flush()
		lastlen = 0
		sys.stdout.write('[%d]failed %s [%d]\n'%(trycnt,cmds,exitcode))
		trycnt += 1
	sys.exit(0)
	return


def main():
	commandline='''
	{
		"verbose|v" : "+",
		"failfast|f" : false,
		"gitsvnfetch<gitsvnfetch_handler>" : {
			"$" : "*"
		},
		"runsucc<runsucc_handler>" : {
			"$" : "+"
		}
	}
	'''

	parser = extargsparse.ExtArgsParse()
	parser.load_command_line_string(commandline)
	args = parser.parse_command_line(None,parser)
	raise Exception('can not accept args')


main()
