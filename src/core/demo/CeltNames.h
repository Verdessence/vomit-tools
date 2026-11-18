//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: CELT 0.11.3's global names, each renamed: libopus has a later CELT inside it with
//          many of the same names, and both are linked into one program. CELT's own sources
//          are compiled with this included first (cmake/Dependencies.cmake), and Voice.cpp
//          includes it before celt.h, so both sides agree on the new names.
//
//===========================================================================//
#pragma once

#define alg_quant xv_celt011_alg_quant
#define alg_unquant xv_celt011_alg_unquant
#define amp2Log2 xv_celt011_amp2Log2
#define anti_collapse xv_celt011_anti_collapse
#define _celt_autocorr xv_celt011__celt_autocorr
#define celt_decode xv_celt011_celt_decode
#define celt_decode_float xv_celt011_celt_decode_float
#define celt_decoder_create xv_celt011_celt_decoder_create
#define celt_decoder_create_custom xv_celt011_celt_decoder_create_custom
#define celt_decoder_ctl xv_celt011_celt_decoder_ctl
#define celt_decoder_destroy xv_celt011_celt_decoder_destroy
#define celt_decoder_get_size xv_celt011_celt_decoder_get_size
#define celt_decoder_get_size_custom xv_celt011_celt_decoder_get_size_custom
#define celt_decoder_init xv_celt011_celt_decoder_init
#define celt_decoder_init_custom xv_celt011_celt_decoder_init_custom
#define celt_encode xv_celt011_celt_encode
#define celt_encode_float xv_celt011_celt_encode_float
#define celt_encoder_create xv_celt011_celt_encoder_create
#define celt_encoder_create_custom xv_celt011_celt_encoder_create_custom
#define celt_encoder_ctl xv_celt011_celt_encoder_ctl
#define celt_encoder_destroy xv_celt011_celt_encoder_destroy
#define celt_encoder_get_size xv_celt011_celt_encoder_get_size
#define celt_encoder_get_size_custom xv_celt011_celt_encoder_get_size_custom
#define celt_encoder_init xv_celt011_celt_encoder_init
#define celt_encoder_init_custom xv_celt011_celt_encoder_init_custom
#define celt_header_from_packet xv_celt011_celt_header_from_packet
#define celt_header_init xv_celt011_celt_header_init
#define celt_header_to_packet xv_celt011_celt_header_to_packet
#define _celt_lpc xv_celt011__celt_lpc
#define celt_mode_create xv_celt011_celt_mode_create
#define celt_mode_destroy xv_celt011_celt_mode_destroy
#define celt_mode_info xv_celt011_celt_mode_info
#define celt_strerror xv_celt011_celt_strerror
#define clt_mdct_backward xv_celt011_clt_mdct_backward
#define clt_mdct_clear xv_celt011_clt_mdct_clear
#define clt_mdct_forward xv_celt011_clt_mdct_forward
#define clt_mdct_init xv_celt011_clt_mdct_init
#define compute_allocation xv_celt011_compute_allocation
#define compute_band_energies xv_celt011_compute_band_energies
#define compute_pulse_cache xv_celt011_compute_pulse_cache
#define decode_pulses xv_celt011_decode_pulses
#define denormalise_bands xv_celt011_denormalise_bands
#define ec_dec_bit_logp xv_celt011_ec_dec_bit_logp
#define ec_dec_bits xv_celt011_ec_dec_bits
#define ec_dec_icdf xv_celt011_ec_dec_icdf
#define ec_dec_init xv_celt011_ec_dec_init
#define ec_decode xv_celt011_ec_decode
#define ec_decode_bin xv_celt011_ec_decode_bin
#define ec_dec_uint xv_celt011_ec_dec_uint
#define ec_dec_update xv_celt011_ec_dec_update
#define ec_enc_bit_logp xv_celt011_ec_enc_bit_logp
#define ec_enc_bits xv_celt011_ec_enc_bits
#define ec_enc_done xv_celt011_ec_enc_done
#define ec_enc_icdf xv_celt011_ec_enc_icdf
#define ec_enc_init xv_celt011_ec_enc_init
#define ec_encode xv_celt011_ec_encode
#define ec_encode_bin xv_celt011_ec_encode_bin
#define ec_enc_patch_initial_bits xv_celt011_ec_enc_patch_initial_bits
#define ec_enc_shrink xv_celt011_ec_enc_shrink
#define ec_enc_uint xv_celt011_ec_enc_uint
#define ec_laplace_decode xv_celt011_ec_laplace_decode
#define ec_laplace_encode xv_celt011_ec_laplace_encode
#define ec_tell_frac xv_celt011_ec_tell_frac
#define encode_pulses xv_celt011_encode_pulses
#define fir xv_celt011_fir
#define get_required_bits xv_celt011_get_required_bits
#define haar1 xv_celt011_haar1
#define icwrs xv_celt011_icwrs
#define iir xv_celt011_iir
#define isqrt32 xv_celt011_isqrt32
#define kiss_fft xv_celt011_kiss_fft
#define kiss_fft_alloc xv_celt011_kiss_fft_alloc
#define kiss_fft_alloc_twiddles xv_celt011_kiss_fft_alloc_twiddles
#define kiss_fft_free xv_celt011_kiss_fft_free
#define kiss_ifft xv_celt011_kiss_ifft
#define lcg_rand xv_celt011_lcg_rand
#define log2Amp xv_celt011_log2Amp
#define log2_frac xv_celt011_log2_frac
#define normalise_bands xv_celt011_normalise_bands
#define pitch_downsample xv_celt011_pitch_downsample
#define pitch_search xv_celt011_pitch_search
#define quant_all_bands xv_celt011_quant_all_bands
#define quant_coarse_energy xv_celt011_quant_coarse_energy
#define quant_energy_finalise xv_celt011_quant_energy_finalise
#define quant_fine_energy xv_celt011_quant_fine_energy
#define remove_doubling xv_celt011_remove_doubling
#define renormalise_vector xv_celt011_renormalise_vector
#define spreading_decision xv_celt011_spreading_decision
#define stereo_itheta xv_celt011_stereo_itheta
#define unquant_coarse_energy xv_celt011_unquant_coarse_energy
#define unquant_energy_finalise xv_celt011_unquant_energy_finalise
#define unquant_fine_energy xv_celt011_unquant_fine_energy
