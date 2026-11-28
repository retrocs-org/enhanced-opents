<?php
declare(strict_types=1);
require_once __DIR__ . '/campaign.php';

const ACCOUNT_TOKEN_LIFETIME = 3600;
const ACCOUNT_MAIL_INTERVAL = 120;

function accountMailSettings(): array
{
    $origin = rtrim(webSetting('OPENTS_ACCOUNT_ORIGIN'), '/');
    $from = webSetting('OPENTS_ACCOUNT_MAIL_FROM');
    if (!preg_match('~\Ahttps://[a-z0-9.-]+(?::[0-9]+)?\z~i', $origin)
        || !filter_var($from, FILTER_VALIDATE_EMAIL)
        || preg_match('/[\r\n]/', $from)) {
        throw new RuntimeException('Account mail is not configured.');
    }
    return [$origin, $from];
}

function allowAccountAttempt(PDO $db, string $address): bool
{
    $now = time();
    $key = hash('sha256', $address, true);
    query($db, 'INSERT INTO opents_account_rate (client, attempts, expires_at) VALUES (?, 1, ?)
        ON DUPLICATE KEY UPDATE attempts = IF(expires_at <= ?, 1, attempts + 1),
        expires_at = IF(expires_at <= ?, VALUES(expires_at), expires_at)',
        [$key, $now + 900, $now, $now]);
    $attempts = (int) query($db, 'SELECT attempts FROM opents_account_rate WHERE client = ?', [$key])->fetchColumn();
    if (random_int(1, 32) === 1) {
        query($db, 'DELETE FROM opents_account_rate WHERE expires_at <= ? ORDER BY expires_at LIMIT 500', [$now]);
    }
    return $attempts <= 30;
}

function sendAccountLink(PDO $db, string $email, bool $reset): void
{
    [$origin, $from] = accountMailSettings();
    $now = time();
    $token = bin2hex(random_bytes(32));
    $hash = hash('sha256', $token, true);
    $active = $reset ? 1 : 0;
    $written = query($db, 'UPDATE opents_player SET token_hash = ?, token_expires = ?
        WHERE email = ? AND (password_hash IS NOT NULL) = ?
          AND (token_expires IS NULL OR token_expires <= ?)',
        [$hash, $now + ACCOUNT_TOKEN_LIFETIME, $email, $active,
            $now + ACCOUNT_TOKEN_LIFETIME - ACCOUNT_MAIL_INTERVAL])->rowCount();
    if ($written === 0) {
        return;
    }

    $mode = $reset ? 'reset' : 'verify';
    $subject = $reset ? 'Reset your Enhanced OpenTS password' : 'Activate your Enhanced OpenTS account';
    $url = $origin . '/sign-in.php?mode=' . $mode . '&token=' . $token;
    if (!mail($email, $subject, "Open the link below within one hour.\n\n$url\n\nIf you did not request this, ignore this message.\n",
        ['From' => $from, 'Content-Type' => 'text/plain; charset=UTF-8'])) {
        query($db, 'UPDATE opents_player SET token_hash = NULL, token_expires = NULL WHERE token_hash = ?', [$hash]);
        error_log('Enhanced OpenTS account mail transport rejected a message.');
    }
}

function acceptAccountPassword(PDO $db, string $token, string $password, bool $reset): bool
{
    if (!preg_match('/\A[0-9a-f]{64}\z/', $token)) {
        return false;
    }
    $hash = hash('sha256', $token, true);
    $active = $reset ? 1 : 0;
    return query($db, 'UPDATE opents_player SET password_hash = ?, credential_version = credential_version + 1,
        token_hash = NULL, token_expires = NULL
        WHERE token_hash = ? AND token_expires > ? AND (password_hash IS NOT NULL) = ?',
        [password_hash($password, PASSWORD_DEFAULT), $hash, time(), $active])->rowCount() === 1;
}
