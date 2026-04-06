/**
 * Tests that BoxshClient auto-detects the bundled src/exec/boxsh binary
 * when neither options.boxshPath nor the BOXSH environment variable is set.
 */

import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { BoxshClient } from '../src/client.mjs';

const bundledBoxsh = join(import.meta.dirname, '../src/exec/boxsh');

describe('BoxshClient — default binary path', () => {
    it('bundled binary exists at src/exec/boxsh', () => {
        assert.ok(existsSync(bundledBoxsh), `expected bundled binary at ${bundledBoxsh}`);
    });

    it('spawns successfully without boxshPath or BOXSH env var', async () => {
        // Temporarily remove BOXSH so the bundled path fallback is exercised.
        const saved = process.env['BOXSH'];
        delete process.env['BOXSH'];

        let client;
        try {
            client = new BoxshClient({ workers: 1 });
            const result = await client.exec('echo "default-path-ok"', '/');
            assert.equal(result.exitCode, 0);
            assert.ok(result.stdout.includes('default-path-ok'));
        } finally {
            if (saved !== undefined) process.env['BOXSH'] = saved;
            await client?.close();
        }
    });
});
