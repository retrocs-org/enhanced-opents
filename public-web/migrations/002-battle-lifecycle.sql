-- Apply once to an existing pilot database with writes stopped.
ALTER TABLE wdt_battle
    MODIFY status ENUM('active', 'won', 'draw', 'aborted', 'expired', 'reserved', 'in_progress', 'review') NOT NULL,
    ADD COLUMN started_at BIGINT UNSIGNED NULL AFTER expires_at,
    ADD COLUMN reason VARCHAR(200) NULL AFTER finished_at,
    MODIFY active_territory VARCHAR(32)
        AS (IF(status IN ('active', 'reserved', 'in_progress', 'review'), territory_id, NULL)) PERSISTENT;

UPDATE wdt_battle SET status = 'reserved' WHERE status = 'active';

ALTER TABLE wdt_battle
    MODIFY status ENUM('reserved', 'in_progress', 'review', 'won', 'draw', 'aborted', 'expired') NOT NULL,
    MODIFY active_territory VARCHAR(32)
        AS (IF(status IN ('reserved', 'in_progress', 'review'), territory_id, NULL)) PERSISTENT;
