//
// Created by torq on 11/9/26.
//

/**
 * Calibration data model
 *
 * METHOD: RIGID TERMINATION
 *
 * Physical model: with a perfectly rigid cap at x = 0, the standing-wave
 *  pressure at distance x from the cap is p(x) ∝ cos(k·x), where
 *  k = 2πf/c. Then the 'theoretical' transfer function is
 *
 *  H12_rigid(f) = cos(k·x2) / cos(k·x1)
 *
 *  Any deviation of the 'measured' H12_rigid from this real-valued
 *  prediction is attributed to the electrical bias of the two
 *  microphones / ADC channels. We capture this as a complex
 *  correction factor:
 *
 *  C(f) = H12_measured(f) / H12_theoretical(f)
 *
 *  Future sample measurements are then corrected by:
 *
 *  H12_corrected(f) = H12_raw(f) / C(f)
 *
 * Conventions:
 *   - Cap face at x = 0.
 *   - Mic 1 at x1_to_cap_m (further from cap).
 *   - Mic 2 at x2_to_cap_m (closer to cap).
 *   - x1 > x2 > 0.
 *
 * Persistence: Save/Load plain text key value format (JSON later)
 */

#ifndef SAEC_STUDIO_CALIBRATION_H
#define SAEC_STUDIO_CALIBRATION_H

















#endif //SAEC_STUDIO_CALIBRATION_H
