#pragma once
struct NxFfxBinding { const char* name; const char* kind; unsigned slot; const char* sampler; };
struct NxFfxPass { unsigned effect,pass; const char* file; const NxFfxBinding* bindings; unsigned count; bool packedPyramid; };
static const NxFfxBinding nx_ffx_1_0[]={{"cbOF","uniform",0,""},{"r_input_color","texture",0,""},{"rw_optical_flow_input","image",0,""}};
static const NxFfxBinding nx_ffx_1_1[]={{"cbOF","uniform",0,""},{"cbOF_SPD","uniform",1,""},{"rw_optical_flow_input","image",0,""},{"rw_optical_flow_input_level_1","image",1,""},{"rw_optical_flow_input_level_2","image",2,""},{"rw_optical_flow_input_level_3","image",3,""},{"rw_optical_flow_input_level_4","image",4,""},{"rw_optical_flow_input_level_5","image",5,""},{"rw_optical_flow_input_level_6","image",6,""}};
static const NxFfxBinding nx_ffx_1_2[]={{"cbOF","uniform",0,""},{"r_optical_flow_input","texture",0,""},{"rw_optical_flow_scd_histogram","image",0,""}};
static const NxFfxBinding nx_ffx_1_3[]={{"cbOF","uniform",0,""},{"rw_optical_flow_scd_histogram","image",0,""},{"rw_optical_flow_scd_previous_histogram","image",1,""},{"rw_optical_flow_scd_temp","image",2,""},{"rw_optical_flow_scd_output","image",3,""}};
static const NxFfxBinding nx_ffx_1_4[]={{"cbOF","uniform",0,""},{"r_optical_flow_input","texture",0,""},{"r_optical_flow_previous_input","texture",1,""},{"rw_optical_flow","image",0,""},{"rw_optical_flow_scd_output","image",1,""}};
static const NxFfxBinding nx_ffx_1_5[]={{"cbOF","uniform",0,""},{"r_optical_flow_previous","texture",0,""},{"rw_optical_flow","image",0,""}};
static const NxFfxBinding nx_ffx_1_6[]={{"cbOF","uniform",0,""},{"r_optical_flow_input","texture",0,""},{"r_optical_flow_previous_input","texture",1,""},{"r_optical_flow","texture",2,""},{"rw_optical_flow_next_level","image",0,""},{"rw_optical_flow_scd_output","image",1,""}};
static const NxFfxBinding nx_ffx_2_0[]={{"cbFI","uniform",0,""},{"r_input_depth","texture",0,""},{"r_input_motion_vectors","texture",1,""},{"rw_dilated_motion_vectors","image",0,""},{"rw_dilated_depth","image",1,""},{"rw_reconstructed_depth_previous_frame","image",2,""}};
static const NxFfxBinding nx_ffx_2_1[]={{"cbFI","uniform",0,""},{"r_optical_flow_scd","texture",0,""},{"rw_disocclusion_mask","image",0,""},{"rw_game_motion_vector_field_x","image",1,""},{"rw_game_motion_vector_field_y","image",2,""},{"rw_optical_flow_motion_vector_field_x","image",3,""},{"rw_optical_flow_motion_vector_field_y","image",4,""},{"rw_counters","storage",0,""}};
static const NxFfxBinding nx_ffx_2_2[]={{"cbFI","uniform",0,""},{"r_current_interpolation_source","texture",0,"s_LinearClamp"},{"r_dilated_motion_vectors","texture",1,""},{"r_dilated_depth","texture",2,""},{"r_input_distortion_field","texture",3,"s_LinearClamp"},{"rw_reconstructed_depth_interpolated_frame","image",0,""}};
static const NxFfxBinding nx_ffx_2_3[]={{"cbFI","uniform",0,""},{"r_previous_interpolation_source","texture",0,"s_LinearClamp"},{"r_current_interpolation_source","texture",1,"s_LinearClamp"},{"r_dilated_motion_vectors","texture",2,""},{"r_dilated_depth","texture",3,""},{"r_input_distortion_field","texture",4,"s_LinearClamp"},{"rw_game_motion_vector_field_x","image",0,""},{"rw_game_motion_vector_field_y","image",1,""}};
static const NxFfxBinding nx_ffx_2_4[]={{"cbFI","uniform",0,""},{"r_previous_interpolation_source","texture",0,"s_LinearClamp"},{"r_current_interpolation_source","texture",1,"s_LinearClamp"},{"r_dilated_depth","texture",2,""},{"r_optical_flow","texture",3,""},{"rw_optical_flow_motion_vector_field_x","image",0,""},{"rw_optical_flow_motion_vector_field_y","image",1,""}};
static const NxFfxBinding nx_ffx_2_5[]={{"cbFI","uniform",0,""},{"r_dilated_depth","texture",0,""},{"r_reconstructed_depth_previous_frame","texture",1,""},{"r_reconstructed_depth_interpolated_frame","texture",2,""},{"r_game_motion_vector_field_x","texture",3,""},{"r_game_motion_vector_field_y","texture",4,""},{"r_inpainting_pyramid","texture",5,""},{"r_input_distortion_field","texture",6,"s_LinearClamp"},{"rw_disocclusion_mask","image",0,""}};
static const NxFfxBinding nx_ffx_2_6[]={{"cbFI","uniform",0,""},{"r_previous_interpolation_source","texture",0,"s_LinearClamp"},{"r_current_interpolation_source","texture",1,"s_LinearClamp"},{"r_disocclusion_mask","texture",2,"s_LinearClamp"},{"r_game_motion_vector_field_x","texture",3,""},{"r_game_motion_vector_field_y","texture",4,""},{"r_optical_flow_motion_vector_field_x","texture",5,""},{"r_optical_flow_motion_vector_field_y","texture",6,""},{"r_inpainting_pyramid","texture",7,""},{"r_counters","storage",0,""},{"rw_output","image",0,""}};
static const NxFfxBinding nx_ffx_2_7[]={{"nxPyramid","storage",0,""},{"nxPyramidLayout","uniform",0,""},{"cbFI","uniform",1,""},{"cbInpaintingPyramid","uniform",2,""},{"r_output","texture",0,""},{"rw_counters","storage",1,""}};
static const NxFfxBinding nx_ffx_2_8[]={{"cbFI","uniform",0,""},{"r_current_interpolation_source","texture",0,"s_LinearClamp"},{"r_optical_flow_scd","texture",1,""},{"r_inpainting_pyramid","texture",2,""},{"r_present_backbuffer","texture",3,"s_LinearClamp"},{"rw_output","image",0,""}};
static const NxFfxBinding nx_ffx_2_9[]={{"nxPyramid","storage",0,""},{"nxPyramidLayout","uniform",0,""},{"cbFI","uniform",1,""},{"cbInpaintingPyramid","uniform",2,""},{"r_game_motion_vector_field_x","texture",0,""},{"r_game_motion_vector_field_y","texture",1,""},{"rw_counters","storage",1,""}};
static const NxFfxBinding nx_ffx_2_10[]={{"cbFI","uniform",0,""},{"r_current_interpolation_source","texture",0,"s_LinearClamp"},{"r_disocclusion_mask","texture",1,"s_LinearClamp"},{"r_game_motion_vector_field_x","texture",2,""},{"r_game_motion_vector_field_y","texture",3,""},{"r_optical_flow_motion_vector_field_x","texture",4,""},{"r_optical_flow_motion_vector_field_y","texture",5,""},{"r_inpainting_pyramid","texture",6,""},{"r_present_backbuffer","texture",7,"s_LinearClamp"},{"r_input_distortion_field","texture",8,"s_LinearClamp"},{"rw_output","image",0,""}};
static const NxFfxPass nx_ffx_passes[]={
{1,0,"ffx_opticalflow_prepare_luma_pass",nx_ffx_1_0,3,false},
{1,1,"ffx_opticalflow_compute_luminance_pyramid_pass",nx_ffx_1_1,9,false},
{1,2,"ffx_opticalflow_generate_scd_histogram_pass",nx_ffx_1_2,3,false},
{1,3,"ffx_opticalflow_compute_scd_divergence_pass",nx_ffx_1_3,5,false},
{1,4,"ffx_opticalflow_compute_optical_flow_advanced_pass_v5",nx_ffx_1_4,5,false},
{1,5,"ffx_opticalflow_filter_optical_flow_pass_v5",nx_ffx_1_5,3,false},
{1,6,"ffx_opticalflow_scale_optical_flow_advanced_pass_v5",nx_ffx_1_6,6,false},
{2,0,"ffx_frameinterpolation_reconstruct_and_dilate_pass",nx_ffx_2_0,6,false},
{2,1,"ffx_frameinterpolation_setup_pass",nx_ffx_2_1,8,false},
{2,2,"ffx_frameinterpolation_reconstruct_previous_depth_pass",nx_ffx_2_2,6,false},
{2,3,"ffx_frameinterpolation_game_motion_vector_field_pass",nx_ffx_2_3,8,false},
{2,4,"ffx_frameinterpolation_optical_flow_vector_field_pass",nx_ffx_2_4,7,false},
{2,5,"ffx_frameinterpolation_disocclusion_mask_pass",nx_ffx_2_5,9,false},
{2,6,"ffx_frameinterpolation_pass",nx_ffx_2_6,11,false},
{2,7,"ffx_frameinterpolation_compute_inpainting_pyramid_pass",nx_ffx_2_7,6,true},
{2,8,"ffx_frameinterpolation_inpainting_pass",nx_ffx_2_8,6,false},
{2,9,"ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass",nx_ffx_2_9,7,true},
{2,10,"ffx_frameinterpolation_debug_view_pass",nx_ffx_2_10,11,false},
};
