/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <acpi.h>
#include <vacpi.h>

/**
 * @pre vm != NULL
 * @pre vm->vm_id < CONFIG_MAX_VM_NUM
 */
void build_vrsdp(struct acrn_vm *vm)
{
	struct acpi_table_rsdp rsdp = {
		.signature = ACPI_SIG_RSDP,
		.oem_id = ACPI_OEM_ID,
		.revision = 0x2U,
		.length = ACPI_RSDP_XCHECKSUM_LENGTH,
		.xsdt_physical_address = VIRT_XSDT_ADDR,
	};

	rsdp.checksum = calculate_checksum8(&rsdp, ACPI_RSDP_CHECKSUM_LENGTH);
	rsdp.extended_checksum = calculate_checksum8(&rsdp, ACPI_RSDP_XCHECKSUM_LENGTH);
	/* Copy RSDP table to guest physical memory F segment */
	(void)copy_to_gpa(vm, &rsdp, VIRT_RSDP_ADDR, ACPI_RSDP_XCHECKSUM_LENGTH);
}

static bool vacpi_probe_table(uint64_t address, const char *signature)
{
	struct acpi_table_header *table = (struct acpi_table_header *)address;

	return strncmp(table->signature, signature, ACPI_NAME_SIZE) == 0;
}

/**
 *  @pre vm != NULL
 */
void vacpi_fixup_madt(struct acrn_vm *vm, struct acpi_table_madt *madt)
{
	void *first, *end, *iterator;
	struct acpi_subtable_header *entry;
	uint16_t vcpu_idx = 0U;

	first = madt + 1U;
	end = (void *)madt + madt->header.length;

	for (iterator = first; (iterator) < (end); iterator += entry->length) {
		entry = (struct acpi_subtable_header *)iterator;
		if (entry->length < sizeof(struct acpi_subtable_header)) {
			break;
		}

		if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
			struct acpi_madt_local_apic *processor = (struct acpi_madt_local_apic *)iterator;

			if ((processor->lapic_flags & ACPI_MADT_ENABLED) != 0U) {
				processor->id = vcpu_vlapic(vcpu_from_vid(vm, vcpu_idx))->vapic_id;
				vcpu_idx++;
			}
		}
	}

	madt->header.checksum = 0x0;
	madt->header.checksum = calculate_checksum8(madt, madt->header.length);
}

/**
 *  @pre vm != NULL && src_addr == NULL
 */
void vacpi_fixup(struct acrn_vm *vm, void *src_addr)
{
	struct acpi_table_xsdt *xsdt = (struct acpi_table_xsdt *)(src_addr + (VIRT_XSDT_ADDR - VIRT_ACPI_DATA_ADDR));
	uint32_t i, count;

	stac();

	count = (xsdt->header.length - sizeof(struct acpi_table_header)) / sizeof(uint64_t);
	for (i = 0U; i < count; i++) {
		uint64_t addr = (uint64_t)src_addr + (xsdt->table_offset_entry[i] - VIRT_ACPI_DATA_ADDR);

		if (vacpi_probe_table(addr, ACPI_SIG_MADT)) {
			vacpi_fixup_madt(vm, (struct acpi_table_madt *)addr);
			break;
		}
	}
	clac();
}