/*
    Copyright 2021 codenocold codenocold@qq.com
    Address : https://github.com/codenocold/ctm
    This file is part of the ctm firmware.
    The ctm firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    The ctm firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "trapTraj.h"
#include "motor_hw.h"
#include "util.h"
#include <math.h>

tTraj Traj;

// A sign function where input 0 has positive sign (not 0)
static inline float sign_hard(float val)
{
    return (signbit(val)) ? -1.0f : 1.0f;
}

void TRAJ_plan(float position, float start_position, float start_velocity, float Vmax, float Amax, float Dmax)
{
    TRAJ_plan_axis(&Traj, position, start_position, start_velocity, Vmax, Amax, Dmax);
}

void TRAJ_eval(void)
{
    TRAJ_eval_axis(&Traj);
}

void TRAJ_plan_axis(tTraj *traj, float position, float start_position, float start_velocity,
                    float Vmax, float Amax, float Dmax)
{
    float distance  = position - start_position;           // Distance to travel
    float stop_dist = SQ(start_velocity) / (2.0f * Dmax);  // Minimum stopping distance
    float dXstop    = copysign(stop_dist, start_velocity); // Minimum stopping displacement
    float s         = sign_hard(distance - dXstop);        // Sign of coast velocity (if any)
    traj->acc       = s * Amax;                            // Maximum Acceleration (signed)
    traj->dec       = -s * Dmax;                           // Maximum Deceleration (signed)
    traj->vel       = s * Vmax;                            // Maximum Velocity (signed)

    // If we start with a speed faster than cruising, then we need to decel instead of accel aka "double deceleration move" in the paper
    if ((s * start_velocity) > (s * traj->vel)) {
        traj->acc = -s * Amax;
    }

    // Time to accel/decel to/from Vr (cruise speed)
    traj->t_acc = (traj->vel - start_velocity) / traj->acc;
    traj->t_dec = -traj->vel / traj->dec;

    // Integral of velocity ramps over the full accel and decel times to get
    // minimum displacement required to reach cuising speed
    float dXmin = 0.5f * traj->t_acc * (traj->vel + start_velocity) + 0.5f * traj->t_dec * traj->vel;

    // Are we displacing enough to reach cruising speed?
    if (s * distance < s * dXmin) {
        // Short move (triangle profile)
        traj->vel = s
                   * sqrtf(fmax((traj->dec * SQ(start_velocity) + 2.0f * traj->acc * traj->dec * distance)
                                    / (traj->dec - traj->acc),
                                0.0f));
        traj->t_acc = fmax(0.0f, (traj->vel - start_velocity) / traj->acc);
        traj->t_dec = fmax(0.0f, -traj->vel / traj->dec);
        traj->t_vel = 0.0f;
    } else {
        // Long move (trapezoidal profile)
        traj->t_vel = (distance - dXmin) / traj->vel;
    }

    // Fill in the rest of the values used at evaluation-time
    traj->t_total        = traj->t_acc + traj->t_vel + traj->t_dec;
    traj->start_position = start_position;
    traj->start_velocity = start_velocity;
    traj->end_position   = position;
    traj->acc_distance   = start_position + start_velocity * traj->t_acc
                         + 0.5f * traj->acc * SQ(traj->t_acc); // pos at end of accel phase

    traj->tick         = 0;
    traj->profile_done = false;
}

void TRAJ_eval_axis(tTraj *traj)
{
    if (traj->profile_done) {
        return;
    }

    traj->tick++;
    float t = traj->tick * CURRENT_MEASURE_PERIOD;

    if (t < 0.0f) { // Initial Condition
        traj->Y   = traj->start_position;
        traj->Yd  = traj->start_velocity;
        traj->Ydd = 0.0f;
    } else if (t < traj->t_acc) { // Accelerating
        traj->Y   = traj->start_position + traj->start_velocity * t + 0.5f * traj->acc * SQ(t);
        traj->Yd  = traj->start_velocity + traj->acc * t;
        traj->Ydd = traj->acc;
    } else if (t < traj->t_acc + traj->t_vel) { // Coasting
        traj->Y   = traj->acc_distance + traj->vel * (t - traj->t_acc);
        traj->Yd  = traj->vel;
        traj->Ydd = 0.0f;
    } else if (t < traj->t_total) { // Deceleration
        float td = t - traj->t_total;
        traj->Y   = traj->end_position + 0.5f * traj->dec * SQ(td);
        traj->Yd  = traj->dec * td;
        traj->Ydd = traj->dec;
    } else if (t >= traj->t_total) { // Final Condition
        traj->Y            = traj->end_position;
        traj->Yd           = 0.0f;
        traj->Ydd          = 0.0f;
        traj->profile_done = true;
    }
}
