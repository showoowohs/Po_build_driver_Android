# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@users.sf.net>
# Rewritten to use lists instead of if-statements.

# Each configuration option enables a list of files.

obj-y                             += po_show_hello.o
obj-$(CONFIG_Po_printk)           += po_Kthread.o
obj-y                             += po_seq_file.o
obj-$(CONFIG_PO_write_proc)       += po_write_proc.o
