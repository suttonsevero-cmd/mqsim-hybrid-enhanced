#include "Flash_Block_Manager.h"
#include "../exec/Flash_Parameter_Set.h"
#include "Stats.h"
namespace SSD_Components
{
	unsigned int Block_Pool_Slot_Type::Page_vector_size = 0;
	Flash_Block_Manager_Base::Flash_Block_Manager_Base(GC_and_WL_Unit_Base* gc_and_wl_unit, unsigned int max_allowed_block_erase_count, unsigned int total_concurrent_streams_no,
		unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
		unsigned int block_no_per_plane, unsigned int page_no_per_block)
		: gc_and_wl_unit(gc_and_wl_unit), max_allowed_block_erase_count(max_allowed_block_erase_count), total_concurrent_streams_no(total_concurrent_streams_no),
		channel_count(channel_count), chip_no_per_channel(chip_no_per_channel), die_no_per_chip(die_no_per_chip), plane_no_per_die(plane_no_per_die),
		block_no_per_plane(block_no_per_plane), pages_no_per_block(page_no_per_block)
	{
		plane_manager = new PlaneBookKeepingType***[channel_count];
		for (unsigned int channelID = 0; channelID < channel_count; channelID++) {
			plane_manager[channelID] = new PlaneBookKeepingType**[chip_no_per_channel];
			for (unsigned int chipID = 0; chipID < chip_no_per_channel; chipID++) {
				plane_manager[channelID][chipID] = new PlaneBookKeepingType*[die_no_per_chip];
				for (unsigned int dieID = 0; dieID < die_no_per_chip; dieID++) {
					plane_manager[channelID][chipID][dieID] = new PlaneBookKeepingType[plane_no_per_die];

					//Initialize plane book keeping data structure
					for (unsigned int planeID = 0; planeID < plane_no_per_die; planeID++) {
						plane_manager[channelID][chipID][dieID][planeID].slc_consumption_rate_ewma = 0.0;
						plane_manager[channelID][chipID][dieID][planeID].slc_blocks_allocated_since_last_check = 0;
						plane_manager[channelID][chipID][dieID][planeID].last_slc_rate_sample_timestamp = 0;
						plane_manager[channelID][chipID][dieID][planeID].SLC_max_blocks = Flash_Parameter_Set::Enable_Hybrid_Mode ? static_cast<unsigned int>( static_cast<uint64_t>(block_no_per_plane) * Flash_Parameter_Set::SLC_Block_Ratio  / 100ULL) : 0U;
						plane_manager[channelID][chipID][dieID][planeID].slc_eviction_ever_started = false;
						plane_manager[channelID][chipID][dieID][planeID].first_slc_eviction_time = 0;
						unsigned int total_pages = plane_manager[channelID][chipID][dieID][planeID].SLC_max_blocks * Flash_Parameter_Set::SLC_Page_No_Per_Block
        				+ (block_no_per_plane - plane_manager[channelID][chipID][dieID][planeID].SLC_max_blocks) * pages_no_per_block;
						plane_manager[channelID][chipID][dieID][planeID].Total_pages_count = total_pages;
						plane_manager[channelID][chipID][dieID][planeID].Free_pages_count = total_pages;
						plane_manager[channelID][chipID][dieID][planeID].Valid_pages_count = 0;
						plane_manager[channelID][chipID][dieID][planeID].Invalid_pages_count = 0;
						plane_manager[channelID][chipID][dieID][planeID].Ongoing_erase_operations.clear();
						plane_manager[channelID][chipID][dieID][planeID].Blocks = new Block_Pool_Slot_Type[block_no_per_plane];
						//Initialize block pool for plane
						for (unsigned int blockID = 0; blockID < block_no_per_plane; blockID++) {
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].BlockID = blockID;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Current_page_write_index = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Current_status = Block_Service_Status::IDLE;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Invalid_page_count = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Erase_count = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Holds_mapping_data = false;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Has_ongoing_gc_wl = false;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Erase_transaction = NULL;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Ongoing_user_program_count = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].slc_eviction_timing_active = false;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].slc_eviction_timing_was_proactive = false;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].slc_eviction_timing_start = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].slc_eviction_timing_valid_pages = 0;
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Ongoing_user_read_count = 0;
							Block_Pool_Slot_Type::Page_vector_size = pages_no_per_block / (sizeof(uint64_t) * 8) + (pages_no_per_block % (sizeof(uint64_t) * 8) == 0 ? 0 : 1);
							plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Invalid_page_bitmap = new uint64_t[Block_Pool_Slot_Type::Page_vector_size];
							for (unsigned int i = 0; i < Block_Pool_Slot_Type::Page_vector_size; i++) {
								plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Invalid_page_bitmap[i] = All_VALID_PAGE;
							}

							if (blockID < plane_manager[channelID][chipID][dieID][planeID].SLC_max_blocks) {
								plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Flash_type = Flash_Technology_Type::SLC;
								plane_manager[channelID][chipID][dieID][planeID].Add_to_free_slc_block_pool(&plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID]);
							}
							else
							{
								plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID].Flash_type = Flash_Technology_Type::QLC;
								plane_manager[channelID][chipID][dieID][planeID].Add_to_free_qlc_block_pool(&plane_manager[channelID][chipID][dieID][planeID].Blocks[blockID], false);
							}
							
						}
						plane_manager[channelID][chipID][dieID][planeID].Data_slc_wf = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						plane_manager[channelID][chipID][dieID][planeID].Data_qlc_wf  = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						plane_manager[channelID][chipID][dieID][planeID].Translation_wf = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						plane_manager[channelID][chipID][dieID][planeID].GC_wf = new Block_Pool_Slot_Type*[total_concurrent_streams_no];
						for (unsigned int stream_cntr = 0; stream_cntr < total_concurrent_streams_no; stream_cntr++) {
							plane_manager[channelID][chipID][dieID][planeID].Data_slc_wf[stream_cntr] =Flash_Parameter_Set::Enable_Hybrid_Mode? plane_manager[channelID][chipID][dieID][planeID].Get_a_free_slc_block(stream_cntr, false): NULL;
							plane_manager[channelID][chipID][dieID][planeID].Data_qlc_wf[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_qlc_block(stream_cntr, false);
							plane_manager[channelID][chipID][dieID][planeID].Translation_wf[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_qlc_block(stream_cntr, true);
							plane_manager[channelID][chipID][dieID][planeID].GC_wf[stream_cntr] = plane_manager[channelID][chipID][dieID][planeID].Get_a_free_qlc_block(stream_cntr, false);
						}
					}
				}
			}
		}
	}

	Flash_Block_Manager_Base::~Flash_Block_Manager_Base() 
	{
		for (unsigned int channel_id = 0; channel_id < channel_count; channel_id++) {
			for (unsigned int chip_id = 0; chip_id < chip_no_per_channel; chip_id++) {
				for (unsigned int die_id = 0; die_id < die_no_per_chip; die_id++) {
					for (unsigned int plane_id = 0; plane_id < plane_no_per_die; plane_id++) {
						for (unsigned int blockID = 0; blockID < block_no_per_plane; blockID++) {
							delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Blocks[blockID].Invalid_page_bitmap;
						}
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Blocks;
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].GC_wf;
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Data_slc_wf;
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Data_qlc_wf;
						delete[] plane_manager[channel_id][chip_id][die_id][plane_id].Translation_wf;
					}
					delete[] plane_manager[channel_id][chip_id][die_id];
				}
				delete[] plane_manager[channel_id][chip_id];
			}
			delete[] plane_manager[channel_id];
		}
		delete[] plane_manager;
	}


	void Flash_Block_Manager_Base::Set_GC_and_WL_Unit(GC_and_WL_Unit_Base* gcwl)
	{
		this->gc_and_wl_unit = gcwl;
	}

	void Block_Pool_Slot_Type::Erase()
	{
		Current_page_write_index = 0;
		Invalid_page_count = 0;
		Erase_count++;
		for (unsigned int i = 0; i < Block_Pool_Slot_Type::Page_vector_size; i++) {
			Invalid_page_bitmap[i] = All_VALID_PAGE;
		}
		Stream_id = NO_STREAM;
		Holds_mapping_data = false;
		Erase_transaction = NULL;
	}

	Block_Pool_Slot_Type* PlaneBookKeepingType::Get_a_free_slc_block(stream_id_type stream_id, bool for_mapping_data) {
		if (Free_slc_block_pool.size() == 0) return NULL;
		Block_Pool_Slot_Type* new_block = Free_slc_block_pool.front();
		Free_slc_block_pool.pop();
		new_block->Stream_id = stream_id;
		new_block->Holds_mapping_data = for_mapping_data;
		return new_block;
	}
	
	Block_Pool_Slot_Type* PlaneBookKeepingType::Get_a_free_qlc_block(stream_id_type stream_id, bool for_mapping_data) {
		if (Free_qlc_block_pool.size() == 0) PRINT_ERROR("empty QLC pool!")
		Block_Pool_Slot_Type* new_block = (*Free_qlc_block_pool.begin()).second;
		Free_qlc_block_pool.erase(Free_qlc_block_pool.begin());
		new_block->Stream_id = stream_id;
		new_block->Holds_mapping_data = for_mapping_data;
		Block_usage_history.push(new_block->BlockID);
		return new_block;
	}


	void PlaneBookKeepingType::Check_bookkeeping_correctness(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		if (Total_pages_count != Free_pages_count + Valid_pages_count + Invalid_pages_count) {
			PRINT_ERROR("Inconsistent status in the plane bookkeeping record!")
		}
		if (Free_pages_count == 0) {
			PRINT_ERROR("Plane " << "@" << plane_address.ChannelID << "@" << plane_address.ChipID << "@" << plane_address.DieID << "@" << plane_address.PlaneID << " pool size: " << Get_free_slc_block_pool_size() + Get_free_qlc_block_pool_size() << " ran out of free pages! Bad resource management! It is not safe to continue simulation!");
		}
	}

	unsigned int PlaneBookKeepingType::Get_free_slc_block_pool_size() { return (unsigned int)Free_slc_block_pool.size(); }
	unsigned int PlaneBookKeepingType::Get_free_qlc_block_pool_size() { return (unsigned int)Free_qlc_block_pool.size(); }

	void PlaneBookKeepingType::Add_to_free_slc_block_pool(Block_Pool_Slot_Type* block) { Free_slc_block_pool.push(block); }
	
	void PlaneBookKeepingType::Add_to_free_qlc_block_pool(Block_Pool_Slot_Type* block, bool consider_dynamic_wl) {
    	Free_qlc_block_pool.insert({consider_dynamic_wl ? block->Erase_count : 0, block});
	}
	
	unsigned int Flash_Block_Manager_Base::Get_min_max_erase_difference(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		PlaneBookKeepingType* plane_record =
			&plane_manager
				[plane_address.ChannelID]
				[plane_address.ChipID]
				[plane_address.DieID]
				[plane_address.PlaneID];

		const unsigned int first_qlc_block = plane_record->SLC_max_blocks;

		if (first_qlc_block >= block_no_per_plane) {
			return 0;
		}

		unsigned int min_erased_block = first_qlc_block;
		unsigned int max_erased_block = first_qlc_block;

		for (unsigned int block_id = first_qlc_block;
			block_id < block_no_per_plane;
			block_id++) {

			if (plane_record->Blocks[block_id].Erase_count
				> plane_record->Blocks[max_erased_block].Erase_count) {
				max_erased_block = block_id;
			}

			if (plane_record->Blocks[block_id].Erase_count
				< plane_record->Blocks[min_erased_block].Erase_count) {
				min_erased_block = block_id;
			}
		}

		return plane_record->Blocks[max_erased_block].Erase_count - plane_record->Blocks[min_erased_block].Erase_count;
	}

	flash_block_ID_type Flash_Block_Manager_Base::Get_coldest_block_id(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID];
		unsigned int min_erased_block = plane_record->SLC_max_blocks;
		for (unsigned int i = plane_record->SLC_max_blocks; i < block_no_per_plane; i++) {
			if (plane_record->Blocks[i].Erase_count < plane_record->Blocks[min_erased_block].Erase_count) {
				min_erased_block = i;
			}
		}
		
		return min_erased_block;
	}

	PlaneBookKeepingType* Flash_Block_Manager_Base::Get_plane_bookkeeping_entry(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		return &(plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID]);
	}
	
	bool Flash_Block_Manager_Base::Block_has_ongoing_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		return plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl;
	}
	
	bool Flash_Block_Manager_Base::Can_execute_gc_wl(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		return (plane_record->Blocks[block_address.BlockID].Ongoing_user_program_count + plane_record->Blocks[block_address.BlockID].Ongoing_user_read_count == 0);
	}
	
	void Flash_Block_Manager_Base::GC_WL_started(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		plane_record->Blocks[block_address.BlockID].Has_ongoing_gc_wl = true;
		
	}
	
	void Flash_Block_Manager_Base::program_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType* plane_record =
			&plane_manager
				[page_address.ChannelID]
				[page_address.ChipID]
				[page_address.DieID]
				[page_address.PlaneID];

		Block_Pool_Slot_Type* block =
			&plane_record->Blocks[page_address.BlockID];

		block->Ongoing_user_program_count++;
	}
	
	void Flash_Block_Manager_Base::Read_transaction_issued(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Blocks[page_address.BlockID].Ongoing_user_read_count++;
	}

	void Flash_Block_Manager_Base::Program_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType* plane_record =
			&plane_manager
				[page_address.ChannelID]
				[page_address.ChipID]
				[page_address.DieID]
				[page_address.PlaneID];

		Block_Pool_Slot_Type* block =
			&plane_record->Blocks[page_address.BlockID];

		if (block->Flash_type == Flash_Technology_Type::SLC) {
			if (block->Ongoing_user_program_count <= 0) {
				Stats::SLC_Program_Service_Underflow_Count++;
			}
		}

		block->Ongoing_user_program_count--;
	}

	void Flash_Block_Manager_Base::Read_transaction_serviced(const NVM::FlashMemory::Physical_Page_Address& page_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[page_address.ChannelID][page_address.ChipID][page_address.DieID][page_address.PlaneID];
		plane_record->Blocks[page_address.BlockID].Ongoing_user_read_count--;
	}
	
	bool Flash_Block_Manager_Base::Is_having_ongoing_program(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType *plane_record = &plane_manager[block_address.ChannelID][block_address.ChipID][block_address.DieID][block_address.PlaneID];
		return plane_record->Blocks[block_address.BlockID].Ongoing_user_program_count > 0;
	}

	void Flash_Block_Manager_Base::GC_WL_finished(const NVM::FlashMemory::Physical_Page_Address& block_address)
	{
		PlaneBookKeepingType* plane_record =
			&plane_manager
				[block_address.ChannelID]
				[block_address.ChipID]
				[block_address.DieID]
				[block_address.PlaneID];

		Block_Pool_Slot_Type* block =
			&plane_record->Blocks[block_address.BlockID];

		if (block->slc_eviction_timing_active)
		{
			if (Simulator->Time()
				<
				block->slc_eviction_timing_start)
			{
				throw std::logic_error(
					"Invalid SLC eviction elapsed time!");
			}

			const sim_time_type elapsed_time =
				Simulator->Time()
				-
				block->slc_eviction_timing_start;

			if (block->slc_eviction_timing_was_proactive)
			{
				Stats::
					SLC_Proactive_Eviction_Timing_Completed_Count++;

				Stats::
					SLC_Proactive_Eviction_Elapsed_Time_Sum
					+= static_cast<long double>(elapsed_time);

				Stats::
					SLC_Proactive_Eviction_Timing_Valid_Page_Count
					+= block->slc_eviction_timing_valid_pages;
			}
			else
			{
				Stats::
					SLC_Passive_Eviction_Timing_Completed_Count++;

				Stats::
					SLC_Passive_Eviction_Elapsed_Time_Sum
					+= static_cast<long double>(elapsed_time);

				Stats::
					SLC_Passive_Eviction_Timing_Valid_Page_Count
					+= block->slc_eviction_timing_valid_pages;
			}

			block->slc_eviction_timing_active = false;
			block->slc_eviction_timing_was_proactive = false;
			block->slc_eviction_timing_start = 0;
			block->slc_eviction_timing_valid_pages = 0;
		}

		block->Has_ongoing_gc_wl = false;
	}
	
	bool Flash_Block_Manager_Base::Is_page_valid(Block_Pool_Slot_Type* block, flash_page_ID_type page_id)
	{
		const unsigned int bitmap_index = page_id / 64;
		const unsigned int bit_index = page_id % 64;
		const uint64_t bit_mask = ((uint64_t)1) << bit_index;

		return (block->Invalid_page_bitmap[bitmap_index] & bit_mask) == 0;
	}
}
