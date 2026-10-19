#include "Stats.h"


namespace SSD_Components
{
	unsigned long Stats::IssuedReadCMD = 0;
	unsigned long Stats::IssuedCopybackReadCMD = 0;
	unsigned long Stats::IssuedInterleaveReadCMD = 0;
	unsigned long Stats::IssuedMultiplaneReadCMD = 0;
	unsigned long Stats::IssuedMultiplaneCopybackReadCMD = 0;
	unsigned long Stats::IssuedProgramCMD = 0;
	unsigned long Stats::IssuedInterleaveProgramCMD = 0;
	unsigned long Stats::IssuedMultiplaneProgramCMD = 0;
	unsigned long Stats::IssuedMultiplaneCopybackProgramCMD = 0;
	unsigned long Stats::IssuedInterleaveMultiplaneProgramCMD = 0;
	unsigned long Stats::IssuedSuspendProgramCMD = 0;
	unsigned long Stats::IssuedCopybackProgramCMD = 0;
	unsigned long Stats::IssuedEraseCMD = 0;
	unsigned long Stats::IssuedInterleaveEraseCMD = 0;
	unsigned long Stats::IssuedMultiplaneEraseCMD = 0;
	unsigned long Stats::IssuedInterleaveMultiplaneEraseCMD = 0;
	unsigned long Stats::IssuedSuspendEraseCMD = 0;
	unsigned long Stats::Total_flash_reads_for_mapping = 0;
	unsigned long Stats::Total_flash_writes_for_mapping = 0;
	unsigned long Stats::Total_flash_reads_for_mapping_per_stream[MAX_SUPPORT_STREAMS] = { 0 };
	unsigned long Stats::Total_flash_writes_for_mapping_per_stream[MAX_SUPPORT_STREAMS] = { 0 };
	unsigned int***** Stats::Block_erase_histogram;
	unsigned int  Stats::CMT_hits = 0, Stats::readTR_CMT_hits = 0, Stats::writeTR_CMT_hits = 0;
	unsigned int  Stats::CMT_miss = 0, Stats::readTR_CMT_miss = 0, Stats::writeTR_CMT_miss = 0;
	unsigned int  Stats::total_CMT_queries = 0, Stats::total_readTR_CMT_queries = 0, Stats::total_writeTR_CMT_queries = 0;

	unsigned int Stats::Total_gc_executions = 0, Stats::Total_gc_executions_per_stream[MAX_SUPPORT_STREAMS] = { 0 };
	unsigned int Stats::Total_page_movements_for_gc = 0, Stats::Total_gc_page_movements_per_stream[MAX_SUPPORT_STREAMS] = { 0 };

	unsigned int Stats::Total_wl_executions = 0, Stats::Total_wl_executions_per_stream[MAX_SUPPORT_STREAMS] = { 0 };
	unsigned int Stats::Total_page_movements_for_wl = 0, Stats::Total_wl_page_movements_per_stream[MAX_SUPPORT_STREAMS] = { 0 };

	unsigned int Stats::CMT_hits_per_stream[MAX_SUPPORT_STREAMS] = { 0 }, Stats::readTR_CMT_hits_per_stream[MAX_SUPPORT_STREAMS] = { 0 }, Stats::writeTR_CMT_hits_per_stream[MAX_SUPPORT_STREAMS] = { 0 };
	unsigned int Stats::CMT_miss_per_stream[MAX_SUPPORT_STREAMS] = { 0 }, Stats::readTR_CMT_miss_per_stream[MAX_SUPPORT_STREAMS] = { 0 }, Stats::writeTR_CMT_miss_per_stream[MAX_SUPPORT_STREAMS] = { 0 };
	unsigned int Stats::total_CMT_queries_per_stream[MAX_SUPPORT_STREAMS] = { 0 }, Stats::total_readTR_CMT_queries_per_stream[MAX_SUPPORT_STREAMS] = { 0 }, Stats::total_writeTR_CMT_queries_per_stream[MAX_SUPPORT_STREAMS] = { 0 };

	unsigned long long Stats::SLC_Eviction_Count = 0;
	unsigned long long Stats::SLC_Zero_Movement_Eviction_Count = 0;

	unsigned long long Stats::SLC_Admission_Count = 0;
	unsigned long long Stats::QLC_Admission_Count = 0;

	unsigned long long Stats::SLC_Invalidated_By_User_Write_Count = 0;
	unsigned long long Stats::SLC_Evicted_Valid_Page_Count = 0;

	unsigned long long Stats::SLC_Residency_Tracking_Mismatch_Count = 0;

	unsigned long long Stats::Dynamic_Trigger_Blocks_Sum = 0;
	unsigned long long Stats::Dynamic_Trigger_Sample_Count = 0;

	unsigned long long Stats::SLC_Free_Block_Sample_Sum = 0;
	unsigned long long Stats::SLC_Min_Free_Block_Count = 0;

	unsigned long long Stats::SLC_Eviction_Trigger_Count = 0;
	unsigned long long Stats::SLC_Eviction_Check_Count = 0;

	unsigned long long
		Stats::SLC_Eviction_Skip_Ongoing_Erase_Count = 0;

	unsigned long long
		Stats::SLC_Eviction_Skip_Concurrency_Count = 0;

	unsigned long long
		Stats::SLC_Eviction_Skip_QLC_Space_Count = 0;

	unsigned long long
		Stats::SLC_Proactive_Eviction_Count = 0;

	unsigned long long
		Stats::SLC_Passive_Eviction_Count = 0;

	unsigned long long
		Stats::SLC_Passive_Eviction_Timing_Completed_Count = 0;

	unsigned long long
		Stats::SLC_Proactive_Eviction_Timing_Completed_Count = 0;

	unsigned long long
		Stats::SLC_Passive_Eviction_Timing_Valid_Page_Count = 0;

	unsigned long long
		Stats::SLC_Proactive_Eviction_Timing_Valid_Page_Count = 0;

	long double
		Stats::SLC_Passive_Eviction_Elapsed_Time_Sum = 0.0L;

	long double
		Stats::SLC_Proactive_Eviction_Elapsed_Time_Sum = 0.0L;

	unsigned long long
		Stats::SLC_Eviction_No_Victim_Count = 0;

	unsigned long long
		Stats::SLC_Program_Service_Underflow_Count = 0;

	double
		Stats::SLC_Eviction_Valid_Ratio_Sample_Sum = 0.0;

	unsigned long long
		Stats::SLC_Pre_Eviction_Data_Read_Count = 0;

	unsigned long long
		Stats::SLC_Pre_Eviction_Data_Write_Count = 0;

	unsigned long long
		Stats::SLC_Post_Eviction_Data_Read_Count = 0;

	unsigned long long
		Stats::SLC_Post_Eviction_Data_Write_Count = 0;

	unsigned long long
		Stats::QLC_Data_Read_Count = 0;

	unsigned long long
		Stats::QLC_Data_Write_Count = 0;

	long double
		Stats::SLC_Pre_Eviction_Data_Read_Latency_Sum = 0.0L;

	long double
		Stats::SLC_Pre_Eviction_Data_Write_Latency_Sum = 0.0L;

	long double
		Stats::SLC_Post_Eviction_Data_Read_Latency_Sum = 0.0L;

	long double
		Stats::SLC_Post_Eviction_Data_Write_Latency_Sum = 0.0L;

	long double
		Stats::QLC_Data_Read_Latency_Sum = 0.0L;

	long double
		Stats::QLC_Data_Write_Latency_Sum = 0.0L;

	void Stats::Init_stats(unsigned int channel_no, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die, 
		unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int max_allowed_block_erase_count)
	{
		Block_erase_histogram = new unsigned int ****[channel_no];
		for (unsigned int channel_cntr = 0; channel_cntr < channel_no; channel_cntr++) {
			Block_erase_histogram[channel_cntr] = new unsigned int***[chip_no_per_channel];
			for (unsigned int chip_cntr = 0; chip_cntr < chip_no_per_channel; chip_cntr++) {
				Block_erase_histogram[channel_cntr][chip_cntr] = new unsigned int**[die_no_per_chip];
				for (unsigned int die_cntr = 0; die_cntr < die_no_per_chip; die_cntr++) {
					Block_erase_histogram[channel_cntr][chip_cntr][die_cntr] = new unsigned int*[plane_no_per_die];
					for (unsigned int plane_cntr = 0; plane_cntr < plane_no_per_die; plane_cntr++) {
						Block_erase_histogram[channel_cntr][chip_cntr][die_cntr][plane_cntr] = new unsigned int[max_allowed_block_erase_count + 1];
						Block_erase_histogram[channel_cntr][chip_cntr][die_cntr][plane_cntr][0] =block_no_per_plane;
						for (unsigned int i = 1;i <= max_allowed_block_erase_count;++i) {
							Block_erase_histogram[channel_cntr][chip_cntr][die_cntr][plane_cntr][i] = 0;
						}
					}
				}
			}
		}

		IssuedReadCMD = 0; IssuedCopybackReadCMD = 0; IssuedInterleaveReadCMD = 0; IssuedMultiplaneReadCMD = 0; IssuedMultiplaneCopybackReadCMD = 0;
		IssuedProgramCMD = 0; IssuedInterleaveProgramCMD = 0; IssuedMultiplaneProgramCMD = 0; IssuedMultiplaneCopybackProgramCMD = 0; IssuedInterleaveMultiplaneProgramCMD = 0; IssuedSuspendProgramCMD = 0; IssuedCopybackProgramCMD = 0;
		IssuedEraseCMD = 0; IssuedInterleaveEraseCMD = 0; IssuedMultiplaneEraseCMD = 0; IssuedInterleaveMultiplaneEraseCMD = 0;
		IssuedSuspendEraseCMD = 0;
		Total_flash_reads_for_mapping = 0; Total_flash_writes_for_mapping = 0; 
		CMT_hits = 0; readTR_CMT_hits = 0; writeTR_CMT_hits = 0;
		CMT_miss = 0; readTR_CMT_miss = 0; writeTR_CMT_miss = 0;
		total_CMT_queries = 0; total_readTR_CMT_queries = 0; total_writeTR_CMT_queries = 0;

		Total_gc_executions = 0;  Total_page_movements_for_gc = 0;
		Total_wl_executions = 0;  Total_page_movements_for_wl = 0;

		SLC_Eviction_Count = 0;
		SLC_Zero_Movement_Eviction_Count = 0;

		SLC_Evicted_Valid_Page_Count = 0;

		SLC_Eviction_Trigger_Count = 0;
		SLC_Eviction_Check_Count = 0;
		SLC_Eviction_Skip_Ongoing_Erase_Count = 0;
		SLC_Eviction_Skip_Concurrency_Count = 0;
		SLC_Eviction_Skip_QLC_Space_Count = 0;
		SLC_Eviction_No_Victim_Count = 0;

		SLC_Program_Service_Underflow_Count = 0;
		SLC_Proactive_Eviction_Count = 0;
		SLC_Passive_Eviction_Count = 0;
		SLC_Passive_Eviction_Timing_Completed_Count = 0;
		SLC_Proactive_Eviction_Timing_Completed_Count = 0;

		SLC_Passive_Eviction_Timing_Valid_Page_Count = 0;
		SLC_Proactive_Eviction_Timing_Valid_Page_Count = 0;

		SLC_Passive_Eviction_Elapsed_Time_Sum = 0.0L;
		SLC_Proactive_Eviction_Elapsed_Time_Sum = 0.0L;

		SLC_Invalidated_By_User_Write_Count = 0;

		SLC_Residency_Tracking_Mismatch_Count = 0;
		SLC_Admission_Count = 0;
		QLC_Admission_Count = 0;

		Dynamic_Trigger_Blocks_Sum = 0;
		Dynamic_Trigger_Sample_Count = 0;

		SLC_Free_Block_Sample_Sum = 0;
		SLC_Min_Free_Block_Count = 0;

		SLC_Pre_Eviction_Data_Read_Count = 0;
		SLC_Pre_Eviction_Data_Write_Count = 0;
		SLC_Post_Eviction_Data_Read_Count = 0;
		SLC_Post_Eviction_Data_Write_Count = 0;

		QLC_Data_Read_Count = 0;
		QLC_Data_Write_Count = 0;

		SLC_Pre_Eviction_Data_Read_Latency_Sum = 0.0L;
		SLC_Pre_Eviction_Data_Write_Latency_Sum = 0.0L;
		SLC_Post_Eviction_Data_Read_Latency_Sum = 0.0L;
		SLC_Post_Eviction_Data_Write_Latency_Sum = 0.0L;

		QLC_Data_Read_Latency_Sum = 0.0L;
		QLC_Data_Write_Latency_Sum = 0.0L;

		for (stream_id_type stream_id = 0; stream_id < MAX_SUPPORT_STREAMS; stream_id++) {
			Total_flash_reads_for_mapping_per_stream[stream_id] = 0;
			Total_flash_writes_for_mapping_per_stream[stream_id] = 0;
			CMT_hits_per_stream[stream_id] = 0; readTR_CMT_hits_per_stream[stream_id] = 0; writeTR_CMT_hits_per_stream[stream_id] = 0;
			CMT_miss_per_stream[stream_id] = 0; readTR_CMT_miss_per_stream[stream_id] = 0;  writeTR_CMT_miss_per_stream[stream_id] = 0;
			total_CMT_queries_per_stream[stream_id] = 0; total_readTR_CMT_queries_per_stream[stream_id] = 0; total_writeTR_CMT_queries_per_stream[stream_id] = 0;
			Total_gc_executions_per_stream[stream_id] = 0;
			Total_gc_page_movements_per_stream[stream_id] = 0;
			Total_wl_executions_per_stream[stream_id] = 0;
			Total_wl_page_movements_per_stream[stream_id] = 0;
		}
	}

	void Stats::Clear_stats(unsigned int channel_no, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die,
		unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int max_allowed_block_erase_count)
	{
		for (unsigned int channel_cntr = 0; channel_cntr < channel_no; channel_cntr++) {
			for (unsigned int chip_cntr = 0; chip_cntr < chip_no_per_channel; chip_cntr++) {
				for (unsigned int die_cntr = 0; die_cntr < die_no_per_chip; die_cntr++) {
					for (unsigned int plane_cntr = 0; plane_cntr < plane_no_per_die; plane_cntr++) {
						delete[] Block_erase_histogram[channel_cntr][chip_cntr][die_cntr][plane_cntr];
					}
					delete[] Block_erase_histogram[channel_cntr][chip_cntr][die_cntr];
				}
				delete[] Block_erase_histogram[channel_cntr][chip_cntr];
			}
			delete[] Block_erase_histogram[channel_cntr];
		}
		delete[] Block_erase_histogram;
	}
}
