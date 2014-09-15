/*
	Copyright 2012-2014 Benjamin Vedder	benjamin@vedder.se

	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

/*
 * terminal.c
 *
 *  Created on: 26 dec 2013
 *      Author: benjamin
 */

#include "ch.h"
#include "hal.h"
#include "terminal.h"
#include "comm.h"
#include "mcpwm.h"
#include "main.h"
#include "hw.h"

#include <string.h>
#include <stdio.h>

void terminal_process_string(char *str) {
	enum { kMaxArgs = 64 };
	int argc = 0;
	char *argv[kMaxArgs];

	char *p2 = strtok(str, " ");
	while (p2 && argc < kMaxArgs) {
		argv[argc++] = p2;
		p2 = strtok(0, " ");
	}

	if (argc == 0) {
		comm_printf("No command received\n");
		return;
	}

	if (strcmp(argv[0], "ping") == 0) {
		comm_printf("pong\n");
	} else if (strcmp(argv[0], "stop") == 0) {
		mcpwm_set_duty(0);
		comm_printf("Motor stopped\n");
	} else if (strcmp(argv[0], "last_adc_duration") == 0) {
		comm_printf("Latest ADC duration: %.4f ms", (double)(mcpwm_get_last_adc_isr_duration() * 1000.0));
		comm_printf("Latest injected ADC duration: %.4f ms", (double)(mcpwm_get_last_inj_adc_isr_duration() * 1000.0));
		comm_printf("Latest main ADC duration: %.4f ms\n", (double)(main_get_last_adc_isr_duration() * 1000.0));
	} else if (strcmp(argv[0], "kv") == 0) {
		comm_printf("Calculated KV: %.2f rpm/volt\n", (double)mcpwm_get_kv_filtered());
	} else if (strcmp(argv[0], "mem") == 0) {
		size_t n, size;
		n = chHeapStatus(NULL, &size);
		comm_printf("core free memory : %u bytes", chCoreStatus());
		comm_printf("heap fragments   : %u", n);
		comm_printf("heap free total  : %u bytes\n", size);
	} else if (strcmp(argv[0], "threads") == 0) {
		Thread *tp;
		static const char *states[] = {THD_STATE_NAMES};
		comm_printf("    addr    stack prio refs     state           name time    ");
		comm_printf("-------------------------------------------------------------");
		tp = chRegFirstThread();
		do {
			comm_printf("%.8lx %.8lx %4lu %4lu %9s %14s %lu",
					(uint32_t)tp, (uint32_t)tp->p_ctx.r13,
					(uint32_t)tp->p_prio, (uint32_t)(tp->p_refs - 1),
					states[tp->p_state], tp->p_name, (uint32_t)tp->p_time);
			tp = chRegNextThread(tp);
		} while (tp != NULL);
		comm_printf("");
	} else if (strcmp(argv[0], "fault") == 0) {
		comm_print_fault_code(mcpwm_get_fault());
	} else if (strcmp(argv[0], "rpm") == 0) {
		comm_printf("Electrical RPM: %.2f rpm\n", (double)mcpwm_get_rpm());
	} else if (strcmp(argv[0], "tacho") == 0) {
		comm_printf("Tachometer counts: %i\n", mcpwm_get_tachometer_value(0));
	} else if (strcmp(argv[0], "tim") == 0) {
		chSysLock();
		volatile int t1_cnt = TIM1->CNT;
		volatile int t8_cnt = TIM8->CNT;
		chSysUnlock();
		int duty = TIM1->CCR1;
		int top = TIM1->ARR;
		int voltage_samp = TIM8->CCR1;
		int current1_samp = TIM1->CCR4;
		int current2_samp = TIM8->CCR2;
		comm_printf("Tim1 CNT: %i", t1_cnt);
		comm_printf("Tim8 CNT: %u", t8_cnt);
		comm_printf("Duty cycle: %u", duty);
		comm_printf("Top: %u", top);
		comm_printf("Voltage sample: %u", voltage_samp);
		comm_printf("Current 1 sample: %u", current1_samp);
		comm_printf("Current 2 sample: %u\n", current2_samp);
	} else if (strcmp(argv[0], "volt") == 0) {
		comm_printf("Input voltage: %.2f\n", (double)GET_INPUT_VOLTAGE());
	} else if (strcmp(argv[0], "param_detect") == 0) {
		// Use COMM_MODE_DELAY and try to figure out the motor parameters.
		if (argc == 4) {
			float current = -1.0;
			float min_rpm = -1.0;
			float low_duty = -1.0;
			sscanf(argv[1], "%f", &current);
			sscanf(argv[2], "%f", &min_rpm);
			sscanf(argv[3], "%f", &low_duty);

			mc_configuration mcconf = mcpwm_get_configuration();

			if (current > 0.0 && current < mcconf.l_current_max &&
					min_rpm > 10.0 && min_rpm < 3000.0 &&
					low_duty > 0.02 && low_duty < 0.8) {

				float cycle_integrator;
				float coupling_k;
				if (conf_general_detect_motor_param(current, min_rpm, low_duty, &cycle_integrator, &coupling_k)) {
					comm_printf("Cycle integrator limit: %.2f", (double)cycle_integrator);
					comm_printf("Coupling factor: %.2f\n", (double)coupling_k);
				} else {
					comm_printf("Detection failed. Try again with different parameters.\n");
				}
			} else {
				comm_printf("Invalid argument(s).\n");
			}
		} else {
			comm_printf("This command requires three arguments.\n");
		}
	} else if (strcmp(argv[0], "rpm_dep") == 0) {
		mc_rpm_dep_struct rpm_dep = mcpwm_get_rpm_dep();
		comm_printf("Cycle int limit: %.2f", (double)rpm_dep.cycle_int_limit);
		comm_printf("Cycle int limit running: %.2f", (double)rpm_dep.cycle_int_limit_running);
		comm_printf("Cycle int limit max: %.2f\n", (double)rpm_dep.cycle_int_limit_max);
	}

	// Setters
	else if (strcmp(argv[0], "set_hall_table") == 0) {
		if (argc == 4) {
			int dir = -1;
			int fwd_add = -1;
			int rev_add = -1;
			sscanf(argv[1], "%i", &dir);
			sscanf(argv[2], "%i", &fwd_add);
			sscanf(argv[3], "%i", &rev_add);

			if (dir >= 0 && fwd_add >= 0 && rev_add >= 0) {
				mcpwm_init_hall_table(dir, fwd_add, rev_add);
				comm_printf("New hall sensor dir: %i fwd_add %i rev_add %i\n",
						dir, fwd_add, rev_add);
			} else {
				comm_printf("Invalid argument(s).\n");
			}
		} else {
			comm_printf("This command requires three arguments.\n");
		}
	}

	// The help command
	else if (strcmp(argv[0], "help") == 0) {
		comm_printf("Valid commands are:");
		comm_printf("help");
		comm_printf("  Show this help");

		comm_printf("ping");
		comm_printf("  Print pong here to see if the reply works");

		comm_printf("stop");
		comm_printf("  Stop the motor");

		comm_printf("last_adc_duration");
		comm_printf("  The time the latest ADC interrupt consumed");

		comm_printf("kv");
		comm_printf("  The calculated kv of the motor");

		comm_printf("mem");
		comm_printf("  Show memory usage");

		comm_printf("threads");
		comm_printf("  List all threads");

		comm_printf("fault");
		comm_printf("  Prints the current fault code");

		comm_printf("rpm");
		comm_printf("  Prints the current electrical RPM");

		comm_printf("tacho");
		comm_printf("  Prints tachometer value");

		comm_printf("tim");
		comm_printf("  Prints tim1 and tim8 settings");

		comm_printf("set_hall_table [dir] [fwd_add] [rev_add]");
		comm_printf("  Update the hall sensor lookup table");

		comm_printf("volt");
		comm_printf("  Prints different voltages");

		comm_printf("param_detect [current] [min_rpm] [low_duty]");
		comm_printf("  Spin up the motor in COMM_MODE_DELAY and compute its parameters.");
		comm_printf("  This test should be performed without load on the motor.");
		comm_printf("  Example: param_detect 5.0 600 0.06");

		comm_printf("rpm_dep");
				comm_printf("  Prints some rpm-dep values\n");
	} else {
		comm_printf("Invalid command: %s\n"
				"type help to list all available commands\n", argv[0]);
	}
}
