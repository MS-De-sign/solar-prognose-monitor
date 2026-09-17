/*
 * Solar Prognose Monitor
 * Copyright (C) 2026 Marcus Sonntag / MS-De-sign
 *
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (C) 2026 Marcus Sonntag / MS-De-sign.
 *
 * Licensed under the PolyForm Noncommercial License 1.0.0 for permitted
 * noncommercial purposes. Commercial use requires separate written permission
 * or a commercial license agreement from the project maintainer.
 *
 * This software is provided without warranty or condition.
 * See the LICENSE file for details. Installation and use are at your own risk.
 *
 * This independent project is not affiliated with, endorsed by, or sponsored
 * by Sungrow Power Supply Co., Ltd. Sungrow trademarks belong to their owners.
 */

#include "SolarPrognoseMonitor.h"

void setup() {
  solarPrognoseMonitorSetup();
}

void loop() {
  solarPrognoseMonitorLoop();
}
