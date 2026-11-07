// SPDX-License-Identifier: GPL-2.0-or-later
export class LobbyError extends Error {
  readonly code: string;
  readonly details?: Record<string, string>;
  constructor(code: string, message: string, details?: Record<string, string>) {
    super(message);
    this.name = 'LobbyError';
    this.code = code;
    this.details = details;
  }
}
