/* Original work Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
 *
 * Modified work Copyright 2025 dobrishinov
 * https://github.com/dobrishinov
 * Note: I hereby authorize the use of my modifications in this code within the premium firmware,
 * without any limitations on its application, including for closed-source or commercial purposes.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#ifndef APP_UART_H
#define APP_UART_H

#include <stdbool.h>

#if defined(ENABLE_MESSENGER) && defined(ENABLE_MESSENGER_UART)
void UART_IsSMSAvailable(void);
#endif
bool UART_IsCommandAvailable(void);
void UART_HandleCommand(void);

#endif

