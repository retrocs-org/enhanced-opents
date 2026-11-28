<?php
declare(strict_types=1);

if (PHP_SAPI !== 'cli') {
    http_response_code(404);
    exit;
}

ini_set('display_errors', '0');
require __DIR__ . '/campaign.php';

try {
    $db = database();
    $battleColumns = $db->query("
        SELECT COLUMN_NAME FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'wdt_battle'
    ")->fetchAll(PDO::FETCH_COLUMN);
    if ($battleColumns && !in_array('started_at', $battleColumns, true)) {
        fwrite(STDERR, "The pilot schema requires migrations 002 and 003. See public-web/README.md.\n");
        exit(1);
    }

    foreach (['schema.sql', 'migrations/005-player-accounts.sql'] as $file) {
        $schema = @file_get_contents(__DIR__ . '/' . $file);
        if ($schema === false || trim($schema) === '') {
            throw new RuntimeException("Missing or empty schema file: $file.");
        }
        foreach (preg_split('/;[ \t]*(?:\R|$)/', $schema) as $statement) {
            if (trim($statement) !== '') {
                $db->exec($statement);
            }
        }
    }
    echo "Enhanced OpenTS MariaDB schema initialized.\n";
} catch (PDOException $error) {
    $message = match ((int) ($error->errorInfo[1] ?? 0)) {
        1044, 1142, 1143 => 'Check the application account grants, including temporary CREATE access.',
        1045 => 'Check the application credentials and account host.',
        1049 => 'Create the configured database before running this command.',
        default => 'Check pdo_mysql, the server connection, and database permissions.',
    };
    fwrite(STDERR, "Schema initialization failed: $message\n");
    exit(1);
} catch (RuntimeException $error) {
    fwrite(STDERR, $error->getMessage() . "\n");
    exit(1);
}
