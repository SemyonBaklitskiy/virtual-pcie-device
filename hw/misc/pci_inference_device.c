#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define TYPE_PCI_CUSTOM_DEVICE "pci-inference-device"
#define PCI_INFERENCE_DEVICE_VENDOR_ID 0xCAFE

/* This macro provides the instance type cast functions for a QOM type */
DECLARE_INSTANCE_CHECKER(struct PciInferenceDevice, INFERENCEDEV, TYPE_PCI_CUSTOM_DEVICE);

struct ControlBitfields
{
	uint32_t start : 1,
		stop : 1,
		reset : 1,
		reserved : 29;
};

struct StatusBitfields
{
	uint32_t busy : 1,
		done : 1,
		error : 4,
		reserved : 26;
};

union Control
{
	uint32_t value;
	struct ControlBitfields bitfields;
};

union Status
{
	uint32_t value;
	struct StatusBitfields bitfields;
};
struct RegisterSpace
{
	union Control control;					 /* RW  */
	union Control control_w1s;				 /* W1S */
	union Control control_w1c;				 /* W1C */
	union Status status;					 /* RO  */
	uint32_t padding[48 / sizeof(uint32_t)]; /* Want to make sizeof(struct RegisterSpace) = 64 bytes */
};

struct PciInferenceDevice
{
	PCIDevice pdev;
	MemoryRegion mmio_bar0; /* register space */
	MemoryRegion mmio_bar1; /* input data */
	MemoryRegion mmio_bar2; /* output data */
	struct RegisterSpace regspace;
	uint8_t input_data[4096];
	uint8_t output_data[4096];
};

static void start_inference(void)
{
	/* TODO: Add inference logic */
	msleep(10);
	printf("Start inference\n");
	return;
}

static void stop_inference(void)
{
	/* TODO: The same */
	printf("Stop inference\n");
	return;
}

static uint64_t
pci_inference_device_bar0_mmio_read(void *ptr, hwaddr offset, uint32_t size)
{
	printf("pci_inference_device_bar0_mmio_read() offset = 0x%lx; size = 0x%x \n", offset, size);

	/* `ptr` was given in memory_region_init_io() function */
	struct PciInferenceDevice *device = ptr;
	uint8_t *base = (uint8_t *)(&device->regspace);

	return *((uint64_t *)(base + offset));
}

static void pci_inference_device_bar0_mmio_write(void *ptr, hwaddr offset, uint64_t value,
												 uint32_t size)
{
	printf("pci_inference_device_bar0_mmio_write() addr = 0x%lx; size = 0x%x; value = 0x%lx\n", offset, size, value);
	struct PciInferenceDevice *device = ptr;

	/* We shouldn't allow to write in RO register */
	if ((offsetof(struct RegisterSpace, status) <= offset) && (offset < offsetof(struct RegisterSpace, status) + sizeof(device->regspace.status)))
	{
		printf("pci_inference_device_bar0_mmio_write() couldn't write to RO register\n");
		return;
	}

	uint8_t *base = (uint8_t *)(&device->regspace);

	*((uint64_t *)(base + offset)) = value;


	if (device->regspace.control.bitfields.reset == 1)
	{
		memset((uint8_t *)(&device->regspace), 0, sizeof(device->regspace));
		memset(&device->input_data, 0, sizeof(device->input_data));
		memset(&device->output_data, 0, sizeof(device->output_data));

		printf("Reset done\n");
	}

	if (device->regspace.control.bitfields.start == 1)
	{
		device->regspace.status.bitfields.busy = 1;
		device->regspace.status.bitfields.done = 0;
		device->regspace.control.bitfields.stop = 0;

		start_inference();

		memset(&device->input_data, 0, sizeof(device->input_data));
		memset(&device->output_data, 1, sizeof(device->output_data));


		device->regspace.status.bitfields.busy = 0;
		device->regspace.status.bitfields.done = 1;
		device->regspace.control.bitfields.start = 0;
		printf("Finish inference\n");
	}
	else if (device->regspace.control.bitfields.stop == 1)
	{
		device->regspace.status.bitfields.busy = 0;
		device->regspace.status.bitfields.done = 0;
		device->regspace.control.bitfields.start = 0;

		stop_inference();

		device->regspace.control.bitfields.stop = 0;
	}
}

static uint64_t
pci_inference_device_bar1_mmio_read(void *ptr, hwaddr offset, uint32_t size)
{
	printf("pci_inference_device_bar1_mmio_read() offset = 0x%lx; size = 0x%x \n", offset, size);

	struct PciInferenceDevice *device = ptr;
	return *((uint64_t *)(&device->input_data + offset));
}

static void pci_inference_device_bar1_mmio_write(void *ptr, hwaddr offset, uint64_t value,
												 uint32_t size)
{
	struct PciInferenceDevice *device = ptr;
	*((uint64_t *)(&device->input_data + offset)) = value;
}

static uint64_t
pci_inference_device_bar2_mmio_read(void *ptr, hwaddr offset, uint32_t size)
{
	printf("pci_inference_device_bar2_mmio_read() offset = 0x%lx; size = 0x%x \n", offset, size);

	struct PciInferenceDevice *device = ptr;
	return *((uint64_t *)(&device->output_data + offset));
}

/* Operations for the Memory Region */
static const MemoryRegionOps bar0_mmio_ops = {
	.read = pci_inference_device_bar0_mmio_read,
	.write = pci_inference_device_bar0_mmio_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 1,
		.max_access_size = 4,
	},
	.impl = {
		.min_access_size = 1,
		.max_access_size = 4,
	},

};

static const MemoryRegionOps bar1_mmio_ops = {
	.read = pci_inference_device_bar1_mmio_read,
	.write = pci_inference_device_bar1_mmio_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 1,
		.max_access_size = 8,
	},
	.impl = {
		.min_access_size = 1,
		.max_access_size = 8,
	},

};

static const MemoryRegionOps bar2_mmio_ops = {
	.read = pci_inference_device_bar2_mmio_read,
	/* .write = NULL since we want RO memory region */
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 1,
		.max_access_size = 8,
	},
	.impl = {
		.min_access_size = 1,
		.max_access_size = 8,
	},
};

/* Implementation of the realize function */
static void pci_inference_device_realize(PCIDevice *pdev, Error **errp)
{
	struct PciInferenceDevice *device = INFERENCEDEV(pdev);
	uint8_t *pci_config = pdev->config;

	pci_config_set_interrupt_pin(pci_config, 1);

	/* Initial configuration of devices registers */
	memset((uint8_t *)(&device->regspace), 0, sizeof(device->regspace));
	memset(&device->input_data, 0, sizeof(device->input_data));
	memset(&device->output_data, 0, sizeof(device->output_data));

	/* Initialize an I/O memory */
	/* Accesses to this region will cause the callbacks */
	/* of the `bar0_mmio_ops` to be called */
	memory_region_init_io(&device->mmio_bar0, OBJECT(device), &bar0_mmio_ops, device, "pci-inference-device-mmio_bar0", sizeof(device->regspace));
	memory_region_init_io(&device->mmio_bar1, OBJECT(device), &bar1_mmio_ops, device, "pci-inference-device-mmio_bar1", sizeof(device->input_data));
	memory_region_init_io(&device->mmio_bar2, OBJECT(device), &bar2_mmio_ops, device, "pci-inference-device-mmio_bar2", sizeof(device->output_data));

	/* Registering the pdev and all of the above configuration */
	/* (actually filling a PCI-IO region with our configuration */
	pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &device->mmio_bar0);
	pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &device->mmio_bar1);
	pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &device->mmio_bar2);
}

static void pci_inference_device_class_init(ObjectClass *class, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(class);
	PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

	/* Definition of realize func() */
	k->realize = pci_inference_device_realize;
	// Definition of uninit func().
	k->vendor_id = PCI_VENDOR_ID_QEMU;
	k->device_id = PCI_INFERENCE_DEVICE_VENDOR_ID; /* Our device id, '0xCAFE' */
	k->revision = 0x0;
	k->class_id = PCI_BASE_CLASS_PROCESSOR; /* For example */
	dc->desc = "PCI Inference Device";

	/**
	 * set_bit - Set a bit in memory
	 * @nr: the bit to set
	 * @addr: the address to start counting from
	 */
	set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static InterfaceInfo interfaces[] = {
	{INTERFACE_CONVENTIONAL_PCI_DEVICE},
	{},
};

static const TypeInfo pci_inference_device_info = {
	.name = TYPE_PCI_CUSTOM_DEVICE,
	.parent = TYPE_PCI_DEVICE,
	.instance_size = sizeof(struct PciInferenceDevice),
	.class_init = pci_inference_device_class_init,
	.interfaces = interfaces,
};

static void pci_inference_device_register_types(void)
{
	/* Register the new type */
	type_register_static(&pci_inference_device_info);
}

type_init(pci_inference_device_register_types)
