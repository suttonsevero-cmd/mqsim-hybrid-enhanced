#ifndef STATS_H
#define STATS_H

#include "SSD_Defs.h"

namespace SSD_Components
{
	class Stats
	{
	public:
		static void Init_stats(unsigned int channel_no, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die, unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int max_allowed_block_erase_count);
		static void Clear_stats(unsigned int channel_no, unsigned int chip_no_per_channel, unsigned int die_no_per_chip, unsigned int plane_no_per_die, unsigned int block_no_per_plane, unsigned int page_no_per_block, unsigned int max_allowed_block_erase_count);
		static unsigned long IssuedReadCMD, IssuedCopybackReadCMD, IssuedInterleaveReadCMD, IssuedMultiplaneReadCMD, IssuedMultiplaneCopybackReadCMD;
		static unsigned long IssuedProgramCMD, IssuedInterleaveProgramCMD, IssuedMultiplaneProgramCMD, IssuedInterleaveMultiplaneProgramCMD, IssuedCopybackProgramCMD, IssuedMultiplaneCopybackProgramCMD;
		static unsigned long IssuedEraseCMD, IssuedInterleaveEraseCMD, IssuedMultiplaneEraseCMD, IssuedInterleaveMultiplaneEraseCMD;

		static unsigned long IssuedSuspendProgramCMD, IssuedSuspendEraseCMD;

		static unsigned long Total_flash_reads_for_mapping, Total_flash_writes_for_mapping;
		static unsigned long Total_flash_reads_for_mapping_per_stream[MAX_SUPPORT_STREAMS], Total_flash_writes_for_mapping_per_stream[MAX_SUPPORT_STREAMS];

		static unsigned int CMT_hits, readTR_CMT_hits, writeTR_CMT_hits;
		static unsigned int CMT_miss, readTR_CMT_miss, writeTR_CMT_miss;
		static unsigned int total_CMT_queries, total_readTR_CMT_queries, total_writeTR_CMT_queries;
		
		static unsigned int CMT_hits_per_stream[MAX_SUPPORT_STREAMS], readTR_CMT_hits_per_stream[MAX_SUPPORT_STREAMS], writeTR_CMT_hits_per_stream[MAX_SUPPORT_STREAMS];
		static unsigned int CMT_miss_per_stream[MAX_SUPPORT_STREAMS], readTR_CMT_miss_per_stream[MAX_SUPPORT_STREAMS], writeTR_CMT_miss_per_stream[MAX_SUPPORT_STREAMS];
		static unsigned int total_CMT_queries_per_stream[MAX_SUPPORT_STREAMS], total_readTR_CMT_queries_per_stream[MAX_SUPPORT_STREAMS], total_writeTR_CMT_queries_per_stream[MAX_SUPPORT_STREAMS];
		

		static unsigned int Total_gc_executions, Total_gc_executions_per_stream[MAX_SUPPORT_STREAMS];
		static unsigned int Total_page_movements_for_gc, Total_gc_page_movements_per_stream[MAX_SUPPORT_STREAMS];

		static unsigned int Total_wl_executions, Total_wl_executions_per_stream[MAX_SUPPORT_STREAMS];
		static unsigned int Total_page_movements_for_wl, Total_wl_page_movements_per_stream[MAX_SUPPORT_STREAMS];

		static unsigned int***** Block_erase_histogram;

		static unsigned long long SLC_Eviction_Count;
		static unsigned long long SLC_Zero_Movement_Eviction_Count;

		static unsigned long long SLC_Eviction_Trigger_Count;
		static unsigned long long SLC_Eviction_Check_Count;

		static unsigned long long
			SLC_Eviction_Skip_Ongoing_Erase_Count;

		static unsigned long long
			SLC_Eviction_Skip_Concurrency_Count;

		static unsigned long long
			SLC_Eviction_Skip_QLC_Space_Count;

		static unsigned long long
			SLC_Proactive_Eviction_Count;

		static unsigned long long
			SLC_Passive_Eviction_Count;

		static unsigned long long
			SLC_Passive_Eviction_Timing_Completed_Count;

		static unsigned long long
			SLC_Proactive_Eviction_Timing_Completed_Count;

		static unsigned long long
			SLC_Passive_Eviction_Timing_Valid_Page_Count;

		static unsigned long long
			SLC_Proactive_Eviction_Timing_Valid_Page_Count;

		static long double
			SLC_Passive_Eviction_Elapsed_Time_Sum;

		static long double
			SLC_Proactive_Eviction_Elapsed_Time_Sum;
			
		static unsigned long long
			SLC_Eviction_No_Victim_Count;

		static unsigned long long
			SLC_Program_Service_Underflow_Count;
		
		static unsigned long long SLC_Admission_Count;
		static unsigned long long QLC_Admission_Count;

		static unsigned long long SLC_Invalidated_By_User_Write_Count;

		static unsigned long long SLC_Residency_Tracking_Mismatch_Count;

		static unsigned long long SLC_Evicted_Valid_Page_Count;

		static unsigned long long SLC_Free_Block_Sample_Sum;
		static unsigned long long SLC_Min_Free_Block_Count;

		static unsigned long long Dynamic_Trigger_Blocks_Sum;
		static unsigned long long Dynamic_Trigger_Sample_Count;

		static double
			SLC_Eviction_Valid_Ratio_Sample_Sum;

		static unsigned long long
			SLC_Pre_Eviction_Data_Read_Count;

		static unsigned long long
			SLC_Pre_Eviction_Data_Write_Count;

		static unsigned long long
			SLC_Post_Eviction_Data_Read_Count;

		static unsigned long long
			SLC_Post_Eviction_Data_Write_Count;

		static unsigned long long
			QLC_Data_Read_Count;

		static unsigned long long
			QLC_Data_Write_Count;

		static long double
			SLC_Pre_Eviction_Data_Read_Latency_Sum;

		static long double
			SLC_Pre_Eviction_Data_Write_Latency_Sum;

		static long double
			SLC_Post_Eviction_Data_Read_Latency_Sum;

		static long double
			SLC_Post_Eviction_Data_Write_Latency_Sum;

		static long double
			QLC_Data_Read_Latency_Sum;

		static long double
			QLC_Data_Write_Latency_Sum;
	};
}

#endif // !STATS_H
