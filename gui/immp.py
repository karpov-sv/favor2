#!/usr/bin/env python
#-*- coding:utf-8 -*-

"""Module for working with the Favor Inter Module Message Protocol
which is described at http://lynx.sao.ru/favor/wiki/InterModuleMessagesFormat
"""

import re


__MESSAGE_FORMAT = re.compile('([a-zA-Z0-9-_]+)=(([a-zA-Z0-9-+,\\._]+)|([\'"])(.*?[^\\\\])\\4)')


class Message:
        def __init__(self, mtype, kwargs={}):
		self.mtype = mtype
		self.kwargs = kwargs

	def __eq__(self, message):
		if message is None: return False
		elif message.mtype != self.mtype: return False
		elif message.kwargs != self.kwargs: return False
		else: return True


        def name(self):
                return self.mtype

        def get(self, key, value=None):
                return self.kwargs.get(key, value)

        def args(self):
                s = []
		for k in self.kwargs:
			v = self.kwargs[k]
			v = v.replace('\\', '\\\\')
			v = v.replace('\"', '\\\"')
			v = v.replace('\'', '\\\'')
			v = v.replace('\t', '\\t')
			s.append(k + '="' + v + '"')
		return ' '.join(s)

        def __str__(self):
		return self.mtype + ' ' + self.args()

def parse(line):
	"""Performs parsing of the IMMP message."""
	chunks = line.split(None, 1)
	if len(chunks) == 0:
		raise ValueError, 'IMMP Message should have type as a first word'
	mtype = chunks[0]
	if len(mtype) == 0:
		raise ValueError, 'IMMP Message type should be a non-empty string'
	kwargs = {}
	if len(chunks) == 2:
		for k, v, _, _, _ in __MESSAGE_FORMAT.findall(chunks[1]):
			v = v[0] in '\'\"' and v[1:-1] or v
			v = v.replace('\\\\', '\\')
			v = v.replace('\\\"', '\"')
			v = v.replace('\\\'', '\'')
			v = v.replace('\\t', '\t')
			kwargs[k] = v
	return Message(mtype, kwargs)

if __name__ == '__main__':
        string = "fast_status acquiring=0"
