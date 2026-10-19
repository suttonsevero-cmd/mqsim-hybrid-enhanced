#include <math.h>
#include <vector>
#include <set>
#include "GC_and_WL_Unit_Page_Level.h"
#include "Flash_Block_Manager.h"
#include "FTL.h"
#include "../exec/Flash_Parameter_Set.h"
#include <limits>
#include <tuple>
#include "Address_Mapping_Unit_Page_Level.h"
namespace SSD_Components
{

	GC_and_WL_Unit_Page_Level::GC_and_WL_Unit_Page_Level(const sim_object_id_type& id,
		Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller, 
		GC_Block_Selection_Policy_Type block_selection_policy, double gc_threshold, bool preemptible_gc_enabled, double gc_hard_threshold,
		unsigned int ChannelCount, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
		unsigned int block_no_per_plane, unsigned int Page_no_per_block, unsigned int sectors_per_page, 
		bool use_copyback, double rho, unsigned int max_ongoing_gc_reqs_per_plane, bool dynamic_wearleveling_enabled, bool static_wearleveling_enabled, unsigned int static_wearleveling_threshold, int seed , bool enable_parity)
		: GC_and_WL_Unit_Base(id, address_mapping_unit, block_manager, tsu, flash_controller, block_selection_policy, gc_threshold, preemptible_gc_enabled, gc_hard_threshold,
		ChannelCount, chip_no_per_channel, die_no_per_chip, plane_no_per_die, block_no_per_plane, Page_no_per_block, sectors_per_page, use_copyback, rho, max_ongoing_gc_reqs_per_plane, 
			dynamic_wearleveling_enabled, static_wearleveling_enabled, static_wearleveling_threshold, seed , enable_parity)
	{
		rga_set_size = (unsigned int)log2(block_no_per_plane);
	}
	
	bool GC_and_WL_Unit_Page_Level::GC_is_in_urgent_mode(const NVM::FlashMemory::Flash_Chip* chip)
	{
		if (!preemptible_gc_enabled) {
			return true;
		}

		NVM::FlashMemory::Physical_Page_Address addr;
		addr.ChannelID = chip->ChannelID; addr.ChipID = chip->ChipID;
		for (unsigned int die_id = 0; die_id < die_no_per_chip; die_id++) {
			for (unsigned int plane_id = 0; plane_id < plane_no_per_die; plane_id++) {
				addr.DieID = die_id; addr.PlaneID = plane_id;
				if (block_manager->Get_pool_size(addr) < block_pool_gc_hard_threshold)
					return true;
			}
		}

		return false;
	}

	void GC_and_WL_Unit_Page_Level::Check_gc_required(const unsigned int free_block_pool_size, const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		if (free_block_pool_size < block_pool_gc_threshold) {
			flash_block_ID_type gc_candidate_block_id = block_manager->Get_coldest_block_id(plane_address);
			PlaneBookKeepingType* pbke = block_manager->Get_plane_bookkeeping_entry(plane_address);

			if (pbke->Ongoing_erase_operations.size() >= max_ongoing_gc_reqs_per_plane) {
				return;
			}

			switch (block_selection_policy) {
				case SSD_Components::GC_Block_Selection_Policy_Type::GREEDY:
				{
					flash_block_ID_type best_candidate =
						block_no_per_plane;

					unsigned int best_invalid_page_count = 0;

					for (flash_block_ID_type block_id =
							pbke->SLC_max_blocks;
						block_id < block_no_per_plane;
						block_id++)
					{
						Block_Pool_Slot_Type* candidate =
							&pbke->Blocks[block_id];

						if (pbke->Ongoing_erase_operations.find(block_id)
							!= pbke->Ongoing_erase_operations.end())
						{
							continue;
						}

						if (!is_safe_gc_wl_candidate(pbke, block_id))
						{
							continue;
						}

						if (candidate->Flash_type
							== Flash_Technology_Type::SLC)
						{
							continue;
						}

						if (candidate->Current_page_write_index
							!= pages_no_per_block)
						{
							continue;
						}

						if (candidate->Invalid_page_count == 0)
						{
							continue;
						}

						if (best_candidate == block_no_per_plane
							|| candidate->Invalid_page_count
								> best_invalid_page_count)
						{
							best_candidate = block_id;
							best_invalid_page_count =
								candidate->Invalid_page_count;
						}
					}

					if (best_candidate == block_no_per_plane)
					{
						return;
					}

					gc_candidate_block_id = best_candidate;
					break;
				}
				case SSD_Components::GC_Block_Selection_Policy_Type::RGA:
				{
					std::set<flash_block_ID_type> random_set;
					while (random_set.size() < rga_set_size) {
						flash_block_ID_type block_id = random_generator.Uniform_uint(pbke->SLC_max_blocks, block_no_per_plane - 1);
						if (pbke->Ongoing_erase_operations.find(block_id) == pbke->Ongoing_erase_operations.end()
							&& is_safe_gc_wl_candidate(pbke, block_id)) {
							random_set.insert(block_id);
							}
					}
					gc_candidate_block_id = *random_set.begin();
					for(auto &block_id : random_set) {
						if (pbke->Blocks[block_id].Invalid_page_count > pbke->Blocks[gc_candidate_block_id].Invalid_page_count
							&& pbke->Blocks[block_id].Current_page_write_index == pages_no_per_block) {
							gc_candidate_block_id = block_id;
						}
					}
					break;
				}
				case SSD_Components::GC_Block_Selection_Policy_Type::RANDOM:
				{
					gc_candidate_block_id = random_generator.Uniform_uint(pbke->SLC_max_blocks, block_no_per_plane - 1);
					unsigned int repeat = 0;

					//A write frontier block should not be selected for garbage collection
					while (!is_safe_gc_wl_candidate(pbke, gc_candidate_block_id) && repeat++ < block_no_per_plane) {
						gc_candidate_block_id = random_generator.Uniform_uint(pbke->SLC_max_blocks, block_no_per_plane - 1);
					}
					break;
				}
				case SSD_Components::GC_Block_Selection_Policy_Type::RANDOM_P:
				{
					gc_candidate_block_id = random_generator.Uniform_uint(pbke->SLC_max_blocks, block_no_per_plane - 1);
					unsigned int repeat = 0;

					//A write frontier block or a block with free pages should not be selected for garbage collection
					while ((pbke->Blocks[gc_candidate_block_id].Current_page_write_index < pages_no_per_block || !is_safe_gc_wl_candidate(pbke, gc_candidate_block_id))
						&& repeat++ < block_no_per_plane) {
						gc_candidate_block_id = random_generator.Uniform_uint(pbke->SLC_max_blocks, block_no_per_plane - 1);
					}
					break;
				}
				case SSD_Components::GC_Block_Selection_Policy_Type::RANDOM_PP:
				{
					gc_candidate_block_id = random_generator.Uniform_uint(pbke->SLC_max_blocks, block_no_per_plane - 1);
					unsigned int repeat = 0;

					//The selected gc block should have a minimum number of invalid pages
					while ((pbke->Blocks[gc_candidate_block_id].Current_page_write_index < pages_no_per_block 
						|| pbke->Blocks[gc_candidate_block_id].Invalid_page_count < random_pp_threshold
						|| !is_safe_gc_wl_candidate(pbke, gc_candidate_block_id))
						&& repeat++ < block_no_per_plane) {
						gc_candidate_block_id = random_generator.Uniform_uint(pbke->SLC_max_blocks, block_no_per_plane - 1);
					}
					break;
				}
				case SSD_Components::GC_Block_Selection_Policy_Type::FIFO:
					gc_candidate_block_id = pbke->Block_usage_history.front();
					pbke->Block_usage_history.pop();
					break;
				default:
					break;
			}

			//This should never happen, but we check it here for safty
			if (pbke->Ongoing_erase_operations.find(gc_candidate_block_id) != pbke->Ongoing_erase_operations.end()) {
				return;
			}
			if (gc_candidate_block_id >= block_no_per_plane)
			{
				return;
			}

			Block_Pool_Slot_Type* block =
				&pbke->Blocks[gc_candidate_block_id];

			if (!is_safe_gc_wl_candidate(
					pbke,
					gc_candidate_block_id))
			{
				return;
			}

			if (block->Flash_type
				== Flash_Technology_Type::SLC)
			{
				return;
			}

			if (block->Current_page_write_index
				!= pages_no_per_block)
			{
				return;
			}

			if (block->Invalid_page_count == 0)
			{
				return;
			}

			if (!Has_qlc_migration_space(pbke,block->Stream_id,block->Current_page_write_index - block->Invalid_page_count)) {
				return;
			}

			NVM::FlashMemory::Physical_Page_Address gc_candidate_address(plane_address);
			gc_candidate_address.BlockID = gc_candidate_block_id;
			gc_candidate_address.is_SLC = false;

			//No invalid page to erase
			if (block->Current_page_write_index == 0 || block->Invalid_page_count == 0) {
				return;
			}
			
			//Run the state machine to protect against race condition
			block_manager->GC_WL_started(gc_candidate_address);
			pbke->Ongoing_erase_operations.insert(gc_candidate_block_id);
			address_mapping_unit->Set_barrier_for_accessing_physical_block(gc_candidate_address);//Lock the block, so no user request can intervene while the GC is progressing
			
			//If there are ongoing requests targeting the candidate block, the gc execution should be postponed
			if (block_manager->Can_execute_gc_wl(gc_candidate_address)) {
				Stats::Total_gc_executions++;
				tsu->Prepare_for_transaction_submit();

				NVM_Transaction_Flash_ER* gc_erase_tr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::GC_WL, pbke->Blocks[gc_candidate_block_id].Stream_id, gc_candidate_address);
				//If there are some valid pages in block, then prepare flash transactions for page movement
				if (block->Current_page_write_index - block->Invalid_page_count > 0) {
					NVM_Transaction_Flash_RD* gc_read = NULL;
					NVM_Transaction_Flash_WR* gc_write = NULL;
					for (flash_page_ID_type pageID = 0; pageID < block->Current_page_write_index; pageID++) {
						if (block_manager->Is_page_valid(block, pageID)) {
							Stats::Total_page_movements_for_gc++;
							gc_candidate_address.PageID = pageID;
							if (use_copyback) {
								gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
									NO_LPA, address_mapping_unit->Convert_address_to_ppa(gc_candidate_address), NULL, 0, NULL, 0, INVALID_TIME_STAMP);
								gc_write->ExecutionMode = WriteExecutionModeType::COPYBACK;
								tsu->Submit_transaction(gc_write);
							} else {
								gc_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
									NO_LPA, address_mapping_unit->Convert_address_to_ppa(gc_candidate_address), gc_candidate_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP);
								gc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
									NO_LPA, NO_PPA, gc_candidate_address, NULL, 0, gc_read, 0, INVALID_TIME_STAMP);
								gc_write->ExecutionMode = WriteExecutionModeType::SIMPLE;
								gc_write->RelatedErase = gc_erase_tr;
								gc_read->RelatedWrite = gc_write;
								tsu->Submit_transaction(gc_read);//Only the read transaction would be submitted. The Write transaction is submitted when the read transaction is finished and the LPA of the target page is determined
							}
							gc_erase_tr->Page_movement_activities.push_back(gc_write);
						}
					}
				}
				block->Erase_transaction = gc_erase_tr;
				tsu->Submit_transaction(gc_erase_tr);

				tsu->Schedule();
			}
		}
	}
	void GC_and_WL_Unit_Page_Level::Check_slc_eviction_required(const NVM::FlashMemory::Physical_Page_Address& plane_address , bool is_proactive_eviction)
	{
		PlaneBookKeepingType* pbke = block_manager->Get_plane_bookkeeping_entry(plane_address);
		Stats::SLC_Eviction_Check_Count++;
		if (pbke->SLC_max_blocks == 0) {
			return;
		}

		for (auto block_id : pbke->Ongoing_erase_operations) {
			if (pbke->Blocks[block_id].Flash_type == Flash_Technology_Type::SLC)
			{
				Stats::SLC_Eviction_Skip_Ongoing_Erase_Count++;
				return;
			}
		}

		if (pbke->Ongoing_erase_operations.size() >= max_ongoing_gc_reqs_per_plane)
		{
			Stats::SLC_Eviction_Skip_Concurrency_Count++;
			return;
		}

		if (pbke->Get_free_qlc_block_pool_size() <= block_pool_gc_threshold)
		{
			Stats::SLC_Eviction_Skip_QLC_Space_Count++;
			return;
		}

		flash_block_ID_type slc_victim_id = pbke->SLC_max_blocks;

		unsigned int best_valid_page_count = std::numeric_limits<unsigned int>::max();
		for (flash_block_ID_type block_id = 0 ; block_id < pbke->SLC_max_blocks; block_id++)
		{
			Block_Pool_Slot_Type* block = &pbke->Blocks[block_id];
			if (block->Current_page_write_index == 0) {
				continue;
			}
			if (pbke->Ongoing_erase_operations.find(block_id) != pbke->Ongoing_erase_operations.end())
			{
				continue;
			}

			if (!is_safe_gc_wl_candidate(pbke, block_id))
			{
				continue;
			}

			const unsigned int valid_page_count =
				static_cast<unsigned int>(block->Current_page_write_index) - block->Invalid_page_count;

			const bool select_candidate =
				slc_victim_id == pbke->SLC_max_blocks
				|| valid_page_count < best_valid_page_count
				|| (valid_page_count == best_valid_page_count
					&& block_id < slc_victim_id);

			if (select_candidate)
			{
				best_valid_page_count = valid_page_count;
				slc_victim_id = block_id;
			}
		}

		if (slc_victim_id == pbke->SLC_max_blocks)
		{
			Stats::SLC_Eviction_No_Victim_Count++;
			return;
		}

		if (!Has_qlc_migration_space(pbke,pbke->Blocks[slc_victim_id].Stream_id,best_valid_page_count)) {
			return;
		}

		if (!pbke->slc_eviction_ever_started)
		{
			pbke->slc_eviction_ever_started = true;
			pbke->first_slc_eviction_time = Simulator->Time();
		}

		if (Flash_Parameter_Set::SLC_Page_No_Per_Block == 0)
		{
			throw std::logic_error(
				"SLC_Page_No_Per_Block is zero "
				"while updating eviction cost feedback!");
		}

		const double victim_valid_ratio =
			static_cast<double>(best_valid_page_count)
			/
			static_cast<double>(Flash_Parameter_Set::SLC_Page_No_Per_Block);

		Stats::SLC_Eviction_Valid_Ratio_Sample_Sum+=victim_valid_ratio;

		if (is_proactive_eviction)
		{
			Stats::SLC_Proactive_Eviction_Count++;
		}
		else
		{
			Stats::SLC_Passive_Eviction_Count++;
		}
		Stats::SLC_Eviction_Count++;
		Stats::SLC_Evicted_Valid_Page_Count += best_valid_page_count;

		if (best_valid_page_count == 0) {
			Stats::SLC_Zero_Movement_Eviction_Count++;
		}

		Block_Pool_Slot_Type* block = &pbke->Blocks[slc_victim_id];

		if (block->slc_eviction_timing_active)
		{
			throw std::logic_error(
				"SLC victim already has active eviction timing!");
		}

		block->slc_eviction_timing_active = true;

		block->slc_eviction_timing_was_proactive =is_proactive_eviction;

		block->slc_eviction_timing_start =Simulator->Time();

		block->slc_eviction_timing_valid_pages =best_valid_page_count;
		NVM::FlashMemory::Physical_Page_Address slc_victim_address(plane_address);
		slc_victim_address.BlockID = slc_victim_id;
		slc_victim_address.is_SLC = true;
		block_manager->GC_WL_started(slc_victim_address);
		pbke->Ongoing_erase_operations.insert(slc_victim_id);
		address_mapping_unit->Set_barrier_for_accessing_physical_block(slc_victim_address);
		
		if (block_manager->Can_execute_gc_wl(slc_victim_address)) {
			Stats::Total_gc_executions++;
			tsu->Prepare_for_transaction_submit();
			NVM_Transaction_Flash_ER* slc_erase_tr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::GC_WL, block->Stream_id, slc_victim_address);
			if (block->Current_page_write_index - block->Invalid_page_count > 0) {
				for (flash_page_ID_type pageID = 0; pageID < block->Current_page_write_index; pageID++) {
					if (block_manager->Is_page_valid(block, pageID)) {
						Stats::Total_page_movements_for_gc++;
						slc_victim_address.PageID = pageID;
						NVM_Transaction_Flash_RD* slc_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
							NO_LPA, address_mapping_unit->Convert_address_to_ppa(slc_victim_address), slc_victim_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP);
						NVM_Transaction_Flash_WR* slc_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
							NO_LPA, NO_PPA, slc_victim_address, NULL, 0, slc_read, 0, INVALID_TIME_STAMP);
						slc_write->ExecutionMode = WriteExecutionModeType::SIMPLE;
						slc_write->RelatedErase = slc_erase_tr;
						slc_read->RelatedWrite = slc_write;
						tsu->Submit_transaction(slc_read);
						slc_erase_tr->Page_movement_activities.push_back(slc_write);
					}
				}
			}
			block->Erase_transaction = slc_erase_tr;
			tsu->Submit_transaction(slc_erase_tr);
			tsu->Schedule();
		}
	}
}
