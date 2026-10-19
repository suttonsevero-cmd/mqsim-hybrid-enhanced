
#include "../nvm_chip/flash_memory/Physical_Page_Address.h"
#include "Flash_Block_Manager.h"
#include "Stats.h"
#include "../exec/Flash_Parameter_Set.h"
#include <sstream>
#include <stdexcept>

namespace SSD_Components
{
	Flash_Block_Manager::Flash_Block_Manager(GC_and_WL_Unit_Base* gc_and_wl_unit, unsigned int max_allowed_block_erase_count, unsigned int total_concurrent_streams_no,unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,unsigned int block_no_per_plane, unsigned int page_no_per_block)
	: Flash_Block_Manager_Base(gc_and_wl_unit, max_allowed_block_erase_count, total_concurrent_streams_no, channel_count, chip_no_per_channel, die_no_per_chip,plane_no_per_die, block_no_per_plane, page_no_per_block)
	{
	}

	Flash_Block_Manager::~Flash_Block_Manager()
	{
	}

	void Flash_Block_Manager::Allocate_block_and_page_in_plane_for_user_write(const stream_id_type stream_id,NVM::FlashMemory::Physical_Page_Address& page_address, bool request_slc, bool is_real_user_write)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Valid_pages_count++;
		plane_record->Free_pages_count--;		
		unsigned int n;
		Block_Pool_Slot_Type* wf = NULL;

		if (request_slc
			&& Flash_Parameter_Set::Enable_Hybrid_Mode
			&& plane_record->SLC_max_blocks > 0)
		{
			wf = plane_record->Data_slc_wf[stream_id];

			if (wf == NULL)
			{
				wf = plane_record->Get_a_free_slc_block(stream_id, false);

				if (wf != NULL)
				{
					plane_record->Data_slc_wf[stream_id] = wf;
					wf->Ongoing_user_program_count = 0;
					plane_record->slc_blocks_allocated_since_last_check++;
				}
			}
		}

		if (wf == NULL)
		{
			wf = plane_record->Data_qlc_wf[stream_id];
		}

		if (wf == NULL)
		{
			PRINT_ERROR("No QLC write frontier is available!")
		}

		page_address.BlockID = wf->BlockID;
		if (is_real_user_write) {
			program_transaction_issued(page_address);
		}

		if (wf->Flash_type == Flash_Technology_Type::SLC)
		{
			page_address.is_SLC = true;
			n = wf->Current_page_write_index++;
			page_address.PageID = n;
		}
		else
		{
			page_address.is_SLC = false;
			if (wf->Current_page_write_index >= pages_no_per_block)
			{
				PRINT_ERROR("QLC user write frontier overflow!")
			}
			n = wf->Current_page_write_index++;
			page_address.PageID = n;
		}

		//The current write frontier block is written to the end
		if (page_address.is_SLC
			&&
			plane_record->Data_slc_wf[stream_id] != NULL
			&&
			plane_record->Data_slc_wf[stream_id]->Current_page_write_index == Flash_Parameter_Set::SLC_Page_No_Per_Block)
		{
			plane_record->Data_slc_wf[stream_id] =plane_record->Get_a_free_slc_block(stream_id,false);

			if (plane_record->Data_slc_wf[stream_id] == NULL)
			{
				gc_and_wl_unit->Check_slc_eviction_required(page_address,false);
			}
			else
			{
				plane_record->slc_blocks_allocated_since_last_check++;
			}
			gc_and_wl_unit->Check_gc_required(plane_record->Get_free_qlc_block_pool_size(),page_address);
		}
		else if (plane_record->Data_qlc_wf[stream_id] != NULL && plane_record->Data_qlc_wf[stream_id]->Current_page_write_index == pages_no_per_block)
		{
			plane_record->Data_qlc_wf[stream_id] = plane_record->Get_a_free_qlc_block(stream_id, false);
			gc_and_wl_unit->Check_gc_required(plane_record->Get_free_qlc_block_pool_size(), page_address);
		}

		plane_record->Check_bookkeeping_correctness(page_address);
	}

	void Flash_Block_Manager::Allocate_block_and_page_in_plane_for_gc_write(const stream_id_type stream_id, NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Valid_pages_count++;
		plane_record->Free_pages_count--;		
		page_address.BlockID = plane_record->GC_wf[stream_id]->BlockID;
		page_address.PageID = plane_record->GC_wf[stream_id]->Current_page_write_index++;
		program_transaction_issued(page_address);

		page_address.is_SLC = false;
		//The current write frontier block is written to the end
		if (plane_record->GC_wf[stream_id]->Current_page_write_index == pages_no_per_block) {
			//Assign a new write frontier block
			plane_record->GC_wf[stream_id] = plane_record->Get_a_free_qlc_block(stream_id, false);
			gc_and_wl_unit->Check_gc_required(plane_record->Get_free_qlc_block_pool_size(), page_address);
		}
		plane_record->Check_bookkeeping_correctness(page_address);
	}
	
	void Flash_Block_Manager::Allocate_Pages_in_block_and_invalidate_remaining_for_preconditioning(const stream_id_type stream_id, const NVM::FlashMemory::Physical_Page_Address& plane_address, std::vector<NVM::FlashMemory::Physical_Page_Address>& page_addresses)
	{
		if(page_addresses.size() > pages_no_per_block) {
			PRINT_ERROR("Error while precondition a physical block: the size of the address list is larger than the pages_no_per_block!")
		}
			
		PlaneBookKeepingType *plane_record = &plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID];
		if (plane_record->Data_qlc_wf[stream_id]->Current_page_write_index > 0) {
			PRINT_ERROR("Illegal operation: the Allocate_Pages_in_block_and_invalidate_remaining_for_preconditioning function should be executed for an erased block!")
		}

		//Assign physical addresses
		for (int i = 0; i < page_addresses.size(); i++) {
			plane_record->Valid_pages_count++;
			plane_record->Free_pages_count--;
			page_addresses[i].BlockID = plane_record->Data_qlc_wf[stream_id]->BlockID;
			page_addresses[i].PageID = plane_record->Data_qlc_wf[stream_id]->Current_page_write_index++;
			plane_record->Check_bookkeeping_correctness(page_addresses[i]);
		}

		//Invalidate the remaining pages in the block
		NVM::FlashMemory::Physical_Page_Address target_address(plane_address);
		while (plane_record->Data_qlc_wf[stream_id]->Current_page_write_index < pages_no_per_block) {
			plane_record->Free_pages_count--;
			target_address.BlockID = plane_record->Data_qlc_wf[stream_id]->BlockID;
			target_address.PageID = plane_record->Data_qlc_wf[stream_id]->Current_page_write_index++;
			Invalidate_page_in_block_for_preconditioning(stream_id, target_address);
			plane_record->Check_bookkeeping_correctness(plane_address);
		}

		//Update the write frontier
		plane_record->Data_qlc_wf[stream_id] = plane_record->Get_a_free_qlc_block(stream_id, false);
	}

	void Flash_Block_Manager::Allocate_block_and_page_in_plane_for_translation_write(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& page_address, bool is_for_gc)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Valid_pages_count++;
		plane_record->Free_pages_count--;
		page_address.BlockID = plane_record->Translation_wf[streamID]->BlockID;
		page_address.is_SLC = false;
		page_address.PageID = plane_record->Translation_wf[streamID]->Current_page_write_index++;
		program_transaction_issued(page_address);

		//The current write frontier block for translation pages is written to the end
		if (plane_record->Translation_wf[streamID]->Current_page_write_index == pages_no_per_block) {
			//Assign a new write frontier block
			plane_record->Translation_wf[streamID] = plane_record->Get_a_free_qlc_block(streamID, true);
			if (!is_for_gc) {
				gc_and_wl_unit->Check_gc_required(plane_record->Get_free_qlc_block_pool_size(), page_address);
			}
		}
		plane_record->Check_bookkeeping_correctness(page_address);
	}

	inline void Flash_Block_Manager::Invalidate_page_in_block(const stream_id_type stream_id, const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType* plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		if (plane_record->Blocks[page_address.BlockID].Stream_id != stream_id) {
			if (plane_record->Blocks[page_address.BlockID].Flash_type == Flash_Technology_Type::SLC) {

				std::ostringstream message;
				message
					<< "SLC invalidation stream mismatch"
					<< ", request_stream=" << stream_id
					<< ", block_stream="
					<< plane_record->Blocks[page_address.BlockID].Stream_id
					<< ", channel=" << page_address.ChannelID
					<< ", chip=" << page_address.ChipID
					<< ", die=" << page_address.DieID
					<< ", plane=" << page_address.PlaneID
					<< ", block=" << page_address.BlockID
					<< ", page=" << page_address.PageID;

				throw std::logic_error(message.str());
			}
			PRINT_ERROR("Inconsistent status in the Invalidate_page_in_block function! The accessed block is not allocated to stream " << stream_id)
		}
		plane_record->Invalid_pages_count++;
		plane_record->Valid_pages_count--;
		plane_record->Blocks[page_address.BlockID].Invalid_page_count++;
		plane_record->Blocks[page_address.BlockID].Invalid_page_bitmap[page_address.PageID / 64] |= ((uint64_t)0x1) << (page_address.PageID % 64);
	}

	inline void Flash_Block_Manager::Invalidate_page_in_block_for_preconditioning(const stream_id_type stream_id, const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType* plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Invalid_pages_count++;
		if (plane_record->Blocks[page_address.BlockID].Stream_id != stream_id) {
			PRINT_ERROR("Inconsistent status in the Invalidate_page_in_block function! The accessed block is not allocated to stream " << stream_id)
		}
		plane_record->Blocks[page_address.BlockID].Invalid_page_count++;
		plane_record->Blocks[page_address.BlockID].Invalid_page_bitmap[page_address.PageID / 64] |= ((uint64_t)0x1) << (page_address.PageID % 64);
	}

	void Flash_Block_Manager::Add_erased_block_to_pool(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		Block_Pool_Slot_Type* block = &(plane_record->Blocks[block_address.BlockID]);
		if (block->Erase_count>= max_allowed_block_erase_count) {
			throw std::runtime_error(
				"Block has reached the maximum allowed "
				"erase count");
		}
		plane_record->Free_pages_count += block->Invalid_page_count;
		plane_record->Invalid_pages_count -= block->Invalid_page_count;

		const unsigned int old_erase_count = block->Erase_count;
		Stats::Block_erase_histogram[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID][old_erase_count]--;
		block->Erase();
		const unsigned int new_erase_count = block->Erase_count;
		if (new_erase_count > max_allowed_block_erase_count) {
			throw std::logic_error(
				"Erase count exceeded histogram capacity");
		}
		Stats::Block_erase_histogram[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID][new_erase_count]++;

		if (block->Flash_type == Flash_Technology_Type::SLC)
		{
			plane_record->Add_to_free_slc_block_pool(block);
		}
		else
		{
			plane_record->Add_to_free_qlc_block_pool(block, gc_and_wl_unit->Use_dynamic_wearleveling());
		}
    	
		
		plane_record->Check_bookkeeping_correctness(block_address);
	}

	inline unsigned int Flash_Block_Manager::Get_pool_size(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		return plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID].Free_qlc_block_pool.size();
	}
}
