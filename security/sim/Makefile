#
# SIM built-in module
#

obj-$(CONFIG_SECURITY_SIM) := sim.o
sim-$(CONFIG_SECURITY_SIM) += sim_util.o

#
# SIM Watchdog
#

sim-$(CONFIG_SECURITY_SIM_WATCHDOG) += sim_watchdog.o
ccflags-y += -Isecurity/sim

# for fs/mount.h
ccflags-y += -Ifs

