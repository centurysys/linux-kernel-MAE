obj-$(CONFIG_QCOM_GSBI)	+=	qcom_gsbi.o
obj-$(CONFIG_QCOM_PM)	+=	spm.o
obj-$(CONFIG_QCOM_SMD) +=	smd.o
obj-$(CONFIG_QCOM_SMD_RPM)	+= smd-rpm.o
obj-$(CONFIG_QCOM_SMEM) +=	smem.o
obj-$(CONFIG_MSM_SMEM)	+=	msm_smem.o
obj-$(CONFIG_MSM_GLINK)	+=	glink.o glink_debugfs.o
obj-$(CONFIG_MSM_GLINK_LOOPBACK_SERVER)	+=	glink_loopback_server.o
obj-$(CONFIG_MSM_GLINK_SMEM_NATIVE_XPRT)+=	glink_smem_native_xprt.o
obj-$(CONFIG_MSM_GLINK_PKT)	+=	msm_glink_pkt.o
obj-$(CONFIG_MSM_IPC_ROUTER_GLINK_XPRT)	+=	ipc_router_glink_xprt.o
obj-$(CONFIG_IPC_ROUTER)	+=	ipc_router/

ifdef CONFIG_QCOM_SCM
obj-$(CONFIG_ARM64) += cpu_ops.o cpu_pwr_ctl.o
endif
