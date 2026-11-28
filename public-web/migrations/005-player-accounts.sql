CREATE TABLE IF NOT EXISTS opents_player (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(24) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL UNIQUE,
    email VARCHAR(254) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL UNIQUE,
    password_hash VARCHAR(255) CHARACTER SET ascii NULL,
    credential_version INT UNSIGNED NOT NULL DEFAULT 0,
    token_hash BINARY(32) NULL UNIQUE,
    token_expires BIGINT UNSIGNED NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS opents_account_rate (
    client BINARY(32) PRIMARY KEY,
    attempts INT UNSIGNED NOT NULL,
    expires_at BIGINT UNSIGNED NOT NULL,
    KEY rate_expiry (expires_at)
) ENGINE=InnoDB;
