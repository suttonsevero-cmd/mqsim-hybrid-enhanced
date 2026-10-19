#include "GC_and_WL_Unit_Base.h"
#include "../exec/Flash_Parameter_Set.h"
#include <cmath>
namespace SSD_Components
{
	GC_and_WL_Unit_Base* GC_and_WL_Unit_Base::_my_instance;
	
	GC_and_WL_Unit_Base::GC_and_WL_Unit_Base(const sim_object_id_type& id,
		Address_Mapping_Unit_Base* address_mapping_unit, Flash_Block_Manager_Base* block_manager, TSU_Base* tsu, NVM_PHY_ONFI* flash_controller,
		GC_Block_Selection_Policy_Type block_selection_policy, double gc_threshold, bool preemptible_gc_enabled, double gc_hard_threshold,
		unsigned int channel_count, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
		unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int sector_no_per_page, 
		bool use_copyback, double rho, unsigned int max_ongoing_gc_reqs_per_plane, bool dynamic_wearleveling_enabled, bool static_wearleveling_enabled, unsigned int static_wearleveling_threshold, int seed , bool enable_parity) :
		Sim_Object(id), address_mapping_unit(address_mapping_unit), block_manager(block_manager), tsu(tsu), flash_controller(flash_controller), force_gc(false),
		block_selection_policy(block_selection_policy), gc_threshold(gc_threshold),	use_copyback(use_copyback), 
		preemptible_gc_enabled(preemptible_gc_enabled), gc_hard_threshold(gc_hard_threshold),
		random_generator(seed), max_ongoing_gc_reqs_per_plane(max_ongoing_gc_reqs_per_plane),
		channel_count(channel_count), chip_no_per_channel(chip_no_per_channel), die_no_per_chip(die_no_per_chip), plane_no_per_die(plane_no_per_die),
		block_no_per_plane(block_no_per_plane), pages_no_per_block(page_no_per_block), sector_no_per_page(sector_no_per_page),
		dynamic_wearleveling_enabled(dynamic_wearleveling_enabled), static_wearleveling_enabled(static_wearleveling_enabled), static_wearleveling_threshold(static_wearleveling_threshold) , enable_parity_restriction(enable_parity)
	{
		_my_instance = this;
		block_pool_gc_threshold = (unsigned int)(gc_threshold * (double)block_no_per_plane);
		if (block_pool_gc_threshold < 1) {
			block_pool_gc_threshold = 1;
		}
		block_pool_gc_hard_threshold = (unsigned int)(gc_hard_threshold * (double)block_no_per_plane);
		if (block_pool_gc_hard_threshold < 1) {
			block_pool_gc_hard_threshold = 1;
		}
		random_pp_threshold = (unsigned int)(rho * pages_no_per_block);
		last_user_io_time = 0;
		timer_running = false;
		consecutive_checks = 0;
		if (block_pool_gc_threshold < max_ongoing_gc_reqs_per_plane) {
			block_pool_gc_threshold = max_ongoing_gc_reqs_per_plane;
		}
	}

	void GC_and_WL_Unit_Base::Setup_triggers()
	{
		Sim_Object::Setup_triggers();
		flash_controller->ConnectToTransactionServicedSignal(handle_transaction_serviced_signal_from_PHY);
	}

	void GC_and_WL_Unit_Base::handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction)
	{
		const bool is_data_transaction =
			transaction->Source == Transaction_Source_Type::USERIO
			||
			transaction->Source == Transaction_Source_Type::CACHE;

		if (is_data_transaction
			&&
			(
				transaction->Type == Transaction_Type::READ
				||
				transaction->Type == Transaction_Type::WRITE
			))
		{
			const long double latency =
				static_cast<long double>(
					Simulator->Time() - transaction->Issue_time);

			if (transaction->Address.is_SLC)
			{
				if (transaction->Type == Transaction_Type::READ)
				{
					if (transaction
						->slc_eviction_started_when_dispatched)
					{
						Stats::SLC_Post_Eviction_Data_Read_Count++;

						Stats::
							SLC_Post_Eviction_Data_Read_Latency_Sum
							+= latency;
					}
					else
					{
						Stats::SLC_Pre_Eviction_Data_Read_Count++;

						Stats::
							SLC_Pre_Eviction_Data_Read_Latency_Sum
							+= latency;
					}
				}
				else
				{
					if (transaction
						->slc_eviction_started_when_dispatched)
					{
						Stats::SLC_Post_Eviction_Data_Write_Count++;

						Stats::
							SLC_Post_Eviction_Data_Write_Latency_Sum
							+= latency;
					}
					else
					{
						Stats::SLC_Pre_Eviction_Data_Write_Count++;

						Stats::
							SLC_Pre_Eviction_Data_Write_Latency_Sum
							+= latency;
					}
				}
			}
			else
			{
				if (transaction->Type == Transaction_Type::READ)
				{
					Stats::QLC_Data_Read_Count++;
					Stats::QLC_Data_Read_Latency_Sum += latency;
				}
				else
				{
					Stats::QLC_Data_Write_Count++;
					Stats::QLC_Data_Write_Latency_Sum += latency;
				}
			}
		}
		
		if (transaction->Source == Transaction_Source_Type::USERIO)
		{
			if (Flash_Parameter_Set::Enable_Hybrid_Mode && !_my_instance->timer_running) {
				//PRINT_MESSAGE("[SLC] timer RESTARTED at " << Simulator->Time());
				_my_instance->timer_running = true;
				Simulator->Register_sim_event(Simulator->Time() + 50000000, _my_instance, NULL, 0);
			}
			_my_instance->last_user_io_time = Simulator->Time();
		}
		PlaneBookKeepingType* pbke = &(_my_instance->block_manager->plane_manager[transaction->Address.ChannelID][transaction->Address.ChipID][transaction->Address.DieID][transaction->Address.PlaneID]);

		switch (transaction->Source) {
			case Transaction_Source_Type::USERIO:
			case Transaction_Source_Type::MAPPING:
			case Transaction_Source_Type::CACHE:
				switch (transaction->Type)
				{
					case Transaction_Type::READ:
						_my_instance->block_manager->Read_transaction_serviced(transaction->Address);
						break;
					case Transaction_Type::WRITE:
						_my_instance->block_manager->Program_transaction_serviced(transaction->Address);
						break;
					default:
						PRINT_ERROR("Unexpected situation in the GC_and_WL_Unit_Base function!")
				}
				if (_my_instance->block_manager->Block_has_ongoing_gc_wl(transaction->Address)) {
					if (_my_instance->block_manager->Can_execute_gc_wl(transaction->Address)) {
						NVM::FlashMemory::Physical_Page_Address gc_wl_candidate_address(transaction->Address);
						Block_Pool_Slot_Type* block = &pbke->Blocks[transaction->Address.BlockID];
						Stats::Total_gc_executions++;
						_my_instance->tsu->Prepare_for_transaction_submit();
						NVM_Transaction_Flash_ER* gc_wl_erase_tr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::GC_WL, block->Stream_id, gc_wl_candidate_address);
						
						//If there are some valid pages in block, then prepare flash transactions for page movement
						if (block->Current_page_write_index - block->Invalid_page_count > 0) {
							//address_mapping_unit->Lock_physical_block_for_gc(gc_candidate_address);//Lock the block, so no user request can intervene while the GC is progressing
							NVM_Transaction_Flash_RD* gc_wl_read = NULL;
							NVM_Transaction_Flash_WR* gc_wl_write = NULL;
							for (flash_page_ID_type pageID = 0; pageID < block->Current_page_write_index; pageID++) {
								if (_my_instance->block_manager->Is_page_valid(block, pageID)) {
									Stats::Total_page_movements_for_gc++;
									gc_wl_candidate_address.PageID = pageID;
									if (_my_instance->use_copyback) {
										gc_wl_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, _my_instance->sector_no_per_page * SECTOR_SIZE_IN_BYTE,
											NO_LPA, _my_instance->address_mapping_unit->Convert_address_to_ppa(gc_wl_candidate_address), NULL, 0, NULL, 0, INVALID_TIME_STAMP);
										gc_wl_write->ExecutionMode = WriteExecutionModeType::COPYBACK;
										_my_instance->tsu->Submit_transaction(gc_wl_write);
									} else {
										gc_wl_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::GC_WL, block->Stream_id, _my_instance->sector_no_per_page * SECTOR_SIZE_IN_BYTE,
											NO_LPA, _my_instance->address_mapping_unit->Convert_address_to_ppa(gc_wl_candidate_address), gc_wl_candidate_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP);
										gc_wl_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, _my_instance->sector_no_per_page * SECTOR_SIZE_IN_BYTE,
											NO_LPA, NO_PPA, gc_wl_candidate_address, NULL, 0, gc_wl_read, 0, INVALID_TIME_STAMP);
										gc_wl_write->ExecutionMode = WriteExecutionModeType::SIMPLE;
										gc_wl_write->RelatedErase = gc_wl_erase_tr;
										gc_wl_read->RelatedWrite = gc_wl_write;
										_my_instance->tsu->Submit_transaction(gc_wl_read);//Only the read transaction would be submitted. The Write transaction is submitted when the read transaction is finished and the LPA of the target page is determined
									}
									gc_wl_erase_tr->Page_movement_activities.push_back(gc_wl_write);
								}
							}
						}
						block->Erase_transaction = gc_wl_erase_tr;
						_my_instance->tsu->Submit_transaction(gc_wl_erase_tr);
						_my_instance->tsu->Schedule();
					}
				}

				if (transaction->Type == Transaction_Type::WRITE
					&& _my_instance->Stop_servicing_writes(
						transaction->Address)
					&& pbke->Ongoing_erase_operations.empty()) {
					_my_instance->Check_gc_required(
						pbke->Get_free_qlc_block_pool_size(),
						transaction->Address);
				}

				return;
		}

		switch (transaction->Type) {
			case Transaction_Type::READ:
			{
				PPA_type ppa;
				MPPN_type mppa;
				page_status_type page_status_bitmap;
				if (pbke->Blocks[transaction->Address.BlockID].Holds_mapping_data) {
					_my_instance->address_mapping_unit->Get_translation_mapping_info_for_gc(transaction->Stream_id, (MVPN_type)transaction->LPA, mppa, page_status_bitmap);
					//There has been no write on the page since GC start, and it is still valid
					if (mppa == transaction->PPA) {
						_my_instance->tsu->Prepare_for_transaction_submit();
						((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->write_sectors_bitmap = FULL_PROGRAMMED_PAGE;
						((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->LPA = transaction->LPA;
						((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
						_my_instance->address_mapping_unit->Allocate_new_page_for_gc(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite, pbke->Blocks[transaction->Address.BlockID].Holds_mapping_data);
						_my_instance->tsu->Submit_transaction(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite);
						_my_instance->tsu->Schedule();
					} else {
							PRINT_ERROR("Inconsistency found when moving a page for GC/WL!");
					}
				} else {
					_my_instance->address_mapping_unit->Get_data_mapping_info_for_gc(transaction->Stream_id, transaction->LPA, ppa, page_status_bitmap);
					
					//There has been no write on the page since GC start, and it is still valid
					if (ppa == transaction->PPA) {
						_my_instance->tsu->Prepare_for_transaction_submit();
						((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->write_sectors_bitmap = page_status_bitmap;
						((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->LPA = transaction->LPA;
						((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite->RelatedRead = NULL;
						_my_instance->address_mapping_unit->Allocate_new_page_for_gc(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite, pbke->Blocks[transaction->Address.BlockID].Holds_mapping_data);
						_my_instance->tsu->Submit_transaction(((NVM_Transaction_Flash_RD*)transaction)->RelatedWrite);
						_my_instance->tsu->Schedule();
					} else {
							PRINT_ERROR("Inconsistency found when moving a page for GC/WL!");
					}
				}
				break;
			}
			case Transaction_Type::WRITE:
				if (static_cast<NVM_Transaction_Flash_WR*>(transaction)->ExecutionMode
					== WriteExecutionModeType::SIMPLE) {
					_my_instance->block_manager->Program_transaction_serviced(
						transaction->Address);
				}

				if (pbke->Blocks[((NVM_Transaction_Flash_WR*)transaction)->RelatedErase->Address.BlockID].Holds_mapping_data) {
					_my_instance->address_mapping_unit->Remove_barrier_for_accessing_mvpn(transaction->Stream_id, (MVPN_type)transaction->LPA);
					DEBUG(Simulator->Time() << ": MVPN=" << (MVPN_type)transaction->LPA << " unlocked!!");
				} else {
					_my_instance->address_mapping_unit->Remove_barrier_for_accessing_lpa(transaction->Stream_id, transaction->LPA);
					DEBUG(Simulator->Time() << ": LPA=" << (MVPN_type)transaction->LPA << " unlocked!!");
				}
				pbke->Blocks[((NVM_Transaction_Flash_WR*)transaction)->RelatedErase->Address.BlockID].Erase_transaction->Page_movement_activities.remove((NVM_Transaction_Flash_WR*)transaction);
				break;
			case Transaction_Type::ERASE:
				pbke->Ongoing_erase_operations.erase(pbke->Ongoing_erase_operations.find(transaction->Address.BlockID));
				_my_instance->block_manager->Add_erased_block_to_pool(transaction->Address);
				_my_instance->block_manager->GC_WL_finished(transaction->Address);
				if (_my_instance->check_static_wl_required(transaction->Address)) {
					_my_instance->run_static_wearleveling(transaction->Address);
				}
				_my_instance->address_mapping_unit->Start_servicing_writes_for_overfull_plane(transaction->Address);//Must be inovked after above statements since it may lead to flash page consumption for waiting program transactions

				if (_my_instance->Stop_servicing_writes(transaction->Address)) {
					_my_instance->Check_gc_required(pbke->Get_free_qlc_block_pool_size(), transaction->Address);
				}
				if (Flash_Parameter_Set::Enable_Hybrid_Mode && !_my_instance->timer_running) {
					_my_instance->timer_running = true;

					Simulator->Register_sim_event(
						Simulator->Time() + 50000000,
						_my_instance,
						NULL,
						0);
				}
				break;
			} //switch (transaction->Type)
	}

	bool GC_and_WL_Unit_Base::Has_qlc_migration_space(PlaneBookKeepingType* plane,stream_id_type stream,uint64_t new_pages) const
	{
		uint64_t available =
			uint64_t(plane->Get_free_qlc_block_pool_size())
			* pages_no_per_block;

		auto* frontier = plane->GC_wf[stream];

		if (frontier != NULL) {
			available +=
				pages_no_per_block
				- frontier->Current_page_write_index;
		}

		uint64_t reserved = 0;

		for (auto id : plane->Ongoing_erase_operations) {
			const auto& victim = plane->Blocks[id];

			if (victim.Erase_transaction == NULL) {
				reserved +=
					victim.Current_page_write_index
					- victim.Invalid_page_count;
			} else {
				for (auto* write :
					victim.Erase_transaction->Page_movement_activities) {
					if (!write->Physical_address_determined) {
						++reserved;
					}
				}
			}
		}

		const uint64_t free_blocks =
			plane->Get_free_qlc_block_pool_size();

		if (free_blocks >= max_ongoing_gc_reqs_per_plane) {
			reserved +=
				(free_blocks - max_ongoing_gc_reqs_per_plane + 1)
				* pages_no_per_block;
		}

		return reserved + new_pages < available;
	}

	void GC_and_WL_Unit_Base::Start_simulation()
	{
		timer_running = false;
		if (Flash_Parameter_Set::Enable_Hybrid_Mode) {
			timer_running = true;
			Simulator->Register_sim_event(
				Simulator->Time() + 50000000,
				this,
				NULL,
				0);
		}
	}

	void GC_and_WL_Unit_Base::Validate_simulation_config()
	{
	}

	void GC_and_WL_Unit_Base::Execute_simulator_event(MQSimEngine::Sim_Event* ev)
	{
		timer_running = false;

		if (!Flash_Parameter_Set::Enable_Hybrid_Mode) {
			return;
		}

		const double EWMA_ALPHA = 0.30;

		const sim_time_type PREDICTION_HORIZON = 100000000;

		const double EMERGENCY_FREE_RATIO = 0.10;

		const double MAX_PROACTIVE_TRIGGER_RATIO = 0.50;

		const unsigned int SAFETY_RESERVE_BLOCKS = 2;

		const sim_time_type now = Simulator->Time();

		NVM::FlashMemory::Physical_Page_Address addr;

		for (unsigned int ch = 0; ch < channel_count; ch++)
		{
			addr.ChannelID = ch;

			for (unsigned int chip = 0;chip < chip_no_per_channel;chip++)
			{
				addr.ChipID = chip;

				for (unsigned int die = 0;die < die_no_per_chip;die++)
				{
					addr.DieID = die;
					for (unsigned int pl = 0;pl < plane_no_per_die;pl++)
					{
						addr.PlaneID = pl;
						PlaneBookKeepingType* pbke = block_manager->Get_plane_bookkeeping_entry(addr);
						if (pbke->SLC_max_blocks == 0) {
							continue;
						}
						sim_time_type elapsed_time;

						if (pbke->last_slc_rate_sample_timestamp == 0) {
							elapsed_time = now;
						}
						else {
							elapsed_time = now - pbke->last_slc_rate_sample_timestamp;
						}

						if (elapsed_time > 0) {
							const double instantaneous_rate = static_cast<double>(pbke->slc_blocks_allocated_since_last_check)/ static_cast<double>(elapsed_time);

							if (pbke->last_slc_rate_sample_timestamp == 0)
							{
								pbke->slc_consumption_rate_ewma = instantaneous_rate;
							}
							else {
								pbke->slc_consumption_rate_ewma = EWMA_ALPHA* instantaneous_rate+ (1.0 - EWMA_ALPHA)* pbke->slc_consumption_rate_ewma;
							}

							pbke ->slc_blocks_allocated_since_last_check = 0;
							pbke->last_slc_rate_sample_timestamp = now;
						}

						const unsigned int free_slc = pbke->Get_free_slc_block_pool_size();

						const unsigned int predicted_block_demand = static_cast<unsigned int>(std::ceil(pbke->slc_consumption_rate_ewma* static_cast<double>(PREDICTION_HORIZON)));

						unsigned int emergency_trigger_blocks = static_cast<unsigned int>(std::ceil(static_cast<double>(pbke->SLC_max_blocks) * EMERGENCY_FREE_RATIO));

						if (emergency_trigger_blocks < 1) {
							emergency_trigger_blocks = 1;
						}

						unsigned int maximum_trigger_blocks = static_cast<unsigned int>(std::ceil(static_cast<double>(pbke->SLC_max_blocks)* MAX_PROACTIVE_TRIGGER_RATIO));

						if (maximum_trigger_blocks < 1) {
							maximum_trigger_blocks = 1;
						}

						unsigned int dynamic_trigger_blocks = predicted_block_demand + SAFETY_RESERVE_BLOCKS;

						if (dynamic_trigger_blocks < emergency_trigger_blocks)
						{
							dynamic_trigger_blocks = emergency_trigger_blocks;
						}

						if (dynamic_trigger_blocks > maximum_trigger_blocks)
						{
							dynamic_trigger_blocks = maximum_trigger_blocks;
						}

						Stats::Dynamic_Trigger_Blocks_Sum += dynamic_trigger_blocks;
						Stats::Dynamic_Trigger_Sample_Count++;
						const bool normal_eviction_required = free_slc <= dynamic_trigger_blocks;
						if (normal_eviction_required)
						{

							Stats::SLC_Eviction_Trigger_Count++;

							if (ENABLE_PROACTIVE_SLC_EVICTION)
							{
								Check_slc_eviction_required(addr, true);
							}
						}
					}
				}
			}
		}
	}

	GC_Block_Selection_Policy_Type GC_and_WL_Unit_Base::Get_gc_policy()
	{
		return block_selection_policy;
	}

	unsigned int GC_and_WL_Unit_Base::Get_GC_policy_specific_parameter()
	{
		switch (block_selection_policy) {
			case GC_Block_Selection_Policy_Type::RGA:
				return rga_set_size;
			case GC_Block_Selection_Policy_Type::RANDOM_PP:
				return random_pp_threshold;
		}

		return 0;
	}

	unsigned int GC_and_WL_Unit_Base::Get_minimum_number_of_free_pages_before_GC()
	{
		return block_pool_gc_threshold;
		/*if (preemptible_gc_enabled)
			return block_pool_gc_hard_threshold;
		else return block_pool_gc_threshold;*/
	}

	bool GC_and_WL_Unit_Base::Use_dynamic_wearleveling()
	{
		return dynamic_wearleveling_enabled;
	}

	inline bool GC_and_WL_Unit_Base::Use_static_wearleveling()
	{
		return static_wearleveling_enabled;
	}
	
	bool GC_and_WL_Unit_Base::Stop_servicing_writes(const NVM::FlashMemory::Physical_Page_Address& plane_address)
	{
		PlaneBookKeepingType* pbke = &(_my_instance->block_manager->plane_manager[plane_address.ChannelID][plane_address.ChipID][plane_address.DieID][plane_address.PlaneID]);
		return block_manager->Get_pool_size(plane_address) < max_ongoing_gc_reqs_per_plane;
	}

	bool GC_and_WL_Unit_Base::is_safe_gc_wl_candidate(const PlaneBookKeepingType* plane_record, const flash_block_ID_type gc_wl_candidate_block_id)
	{
		//The block shouldn't be a current write frontier
		for (unsigned int stream_id = 0; stream_id < address_mapping_unit->Get_no_of_input_streams(); stream_id++) {
			if (
				(&plane_record->Blocks[gc_wl_candidate_block_id])
					== plane_record->Data_slc_wf[stream_id]
				||
				(&plane_record->Blocks[gc_wl_candidate_block_id])
					== plane_record->Data_qlc_wf[stream_id]
				||
				(&plane_record->Blocks[gc_wl_candidate_block_id])
					== plane_record->Translation_wf[stream_id]
				||
				(&plane_record->Blocks[gc_wl_candidate_block_id])
					== plane_record->GC_wf[stream_id])
			{
				return false;
			}

		}

		//The block shouldn't have an ongoing program request (all pages must already be written)
		if (plane_record->Blocks[gc_wl_candidate_block_id].Ongoing_user_program_count > 0) {
			return false;
		}

		if (plane_record->Blocks[gc_wl_candidate_block_id].Has_ongoing_gc_wl) {
			return false;
		}

		return true;
	}

	inline bool GC_and_WL_Unit_Base::check_static_wl_required(const NVM::FlashMemory::Physical_Page_Address plane_address)
	{
		return static_wearleveling_enabled && (block_manager->Get_min_max_erase_difference(plane_address) >= static_wearleveling_threshold);
	}

	void GC_and_WL_Unit_Base::run_static_wearleveling(const NVM::FlashMemory::Physical_Page_Address plane_address)
	{
		PlaneBookKeepingType* pbke = block_manager->Get_plane_bookkeeping_entry(plane_address);
		flash_block_ID_type wl_candidate_block_id = block_manager->Get_coldest_block_id(plane_address);
		if (!is_safe_gc_wl_candidate(pbke, wl_candidate_block_id)) {
			return;
		}

		NVM::FlashMemory::Physical_Page_Address wl_candidate_address(plane_address);
		wl_candidate_address.BlockID = wl_candidate_block_id;
		Block_Pool_Slot_Type* block = &pbke->Blocks[wl_candidate_block_id];

		if (!Has_qlc_migration_space(
				pbke,
				block->Stream_id,
				block->Current_page_write_index
					- block->Invalid_page_count)) {
			return;
		}

		//Run the state machine to protect against race condition
		block_manager->GC_WL_started(wl_candidate_address);
		pbke->Ongoing_erase_operations.insert(wl_candidate_block_id);
		address_mapping_unit->Set_barrier_for_accessing_physical_block(wl_candidate_address);//Lock the block, so no user request can intervene while the GC is progressing
		if (block_manager->Can_execute_gc_wl(wl_candidate_address)) {//If there are ongoing requests targeting the candidate block, the gc execution should be postponed
			Stats::Total_wl_executions++;
			tsu->Prepare_for_transaction_submit();

			NVM_Transaction_Flash_ER* wl_erase_tr = new NVM_Transaction_Flash_ER(Transaction_Source_Type::GC_WL, pbke->Blocks[wl_candidate_block_id].Stream_id, wl_candidate_address);
			if (block->Current_page_write_index - block->Invalid_page_count > 0) {//If there are some valid pages in block, then prepare flash transactions for page movement
				NVM_Transaction_Flash_RD* wl_read = NULL;
				NVM_Transaction_Flash_WR* wl_write = NULL;
				for (flash_page_ID_type pageID = 0; pageID < block->Current_page_write_index; pageID++) {
					if (block_manager->Is_page_valid(block, pageID)) {
						Stats::Total_page_movements_for_wl++;
						Stats::Total_wl_page_movements_per_stream[block->Stream_id]++;
						wl_candidate_address.PageID = pageID;
						if (use_copyback) {
							wl_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, address_mapping_unit->Convert_address_to_ppa(wl_candidate_address), NULL, 0, NULL, 0, INVALID_TIME_STAMP);
							wl_write->ExecutionMode = WriteExecutionModeType::COPYBACK;
							tsu->Submit_transaction(wl_write);
						} else {
							wl_read = new NVM_Transaction_Flash_RD(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, address_mapping_unit->Convert_address_to_ppa(wl_candidate_address), wl_candidate_address, NULL, 0, NULL, 0, INVALID_TIME_STAMP);
							wl_write = new NVM_Transaction_Flash_WR(Transaction_Source_Type::GC_WL, block->Stream_id, sector_no_per_page * SECTOR_SIZE_IN_BYTE,
								NO_LPA, NO_PPA, wl_candidate_address, NULL, 0, wl_read, 0, INVALID_TIME_STAMP);
							wl_write->ExecutionMode = WriteExecutionModeType::SIMPLE;
							wl_write->RelatedErase = wl_erase_tr;
							wl_read->RelatedWrite = wl_write;
							tsu->Submit_transaction(wl_read);//Only the read transaction would be submitted. The Write transaction is submitted when the read transaction is finished and the LPA of the target page is determined
						}
						wl_erase_tr->Page_movement_activities.push_back(wl_write);
					}
				}
			}
			block->Erase_transaction = wl_erase_tr;
			tsu->Submit_transaction(wl_erase_tr);

			tsu->Schedule();
		}
	}
}
